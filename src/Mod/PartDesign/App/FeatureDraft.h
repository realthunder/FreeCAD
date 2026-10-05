/***************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer                                    *
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


#ifndef PARTDESIGN_FEATUREDRAFT_H
#define PARTDESIGN_FEATUREDRAFT_H

#include <gp_Pln.hxx>
#include <gp_Dir.hxx>

#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <App/PropertyLinks.h>
#include "FeatureDressUp.h"

namespace PartDesign
{
struct PartDesignExport DraftComputeProps
{
    gp_Dir pullDirection;
    gp_Pln neutralPlane;
};

class PartDesignExport Draft : public DressUp
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Draft);

public:
    Draft();

    App::PropertyAngle Angle;
    App::PropertyLinkSub NeutralPlane;
    App::PropertyLinkSub PullDirection;
    App::PropertyBool Reversed;
    /** The edge a guessed neutral plane is taken from, and the side of it.
     *
     * With no NeutralPlane given the plane is guessed from an edge of the
     * first face: through the edge, its normal -- the pull direction -- along
     * the face. Which edge, and which of the two ways along the face, decide
     * which way the draft goes, and both came out of how the shape happens
     * to be written down: "the first edge that will do" in the order the face
     * lists them, and the cross product of the edge's own direction with the
     * axis of the face's surface. Nothing promises any of the three. A base
     * recomputed by another kernel version had its edges in another order
     * and its surface the other way up, and the draft of a file turned over:
     * Reversed meant the opposite.
     *
     * So the edge is written down here by its mapped name, and the side in
     * _NeutralSense as it relates to the face itself (1 for into the face
     * from the edge, or with the face's outward normal where the plane is
     * across the face; -1 against). The first guess fills them in, and for
     * a file from before this they are taken from the base's stored shape
     * as the file is restored, which is the last moment it has the form the
     * guess was made on. Hidden; the feature writes them.
     */
    App::PropertyLinkSub _NeutralEdge;
    App::PropertyInteger _NeutralSense;
    App::PropertyEnumeration Method;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn *execute() override;
    short mustExecute() const override;
    void onDocumentRestored() override;
    /// returns the type name of the view provider
    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderDraft";
    }
    //@}

    /**
     * @brief getLastComputedProps: Returns the Pull Direction and Neutral Plane
     * computed during the last call of the execute method.
     * Note: The returned values might be in the default initialized state if
     * they were not computed or computation failed
     */
    DraftComputeProps getLastComputedProps() const
    {
        return computeProps;
    }

private:
    Part::TopoShape refineBase(const Part::TopoShape &baseShape) const;
    void handleChangedPropertyType(Base::XMLReader &reader, const char * TypeName, App::Property * prop) override;
    static const App::PropertyAngle::Constraints floatAngle;

    /// The neutral plane for \a face when none is given; see _NeutralEdge.
    bool guessNeutralPlane(const Part::TopoShape &baseShape,
                           const Part::TopoShape &face,
                           gp_Pln &plane);

    DraftComputeProps computeProps;
};

} //namespace PartDesign


#endif // PARTDESIGN_FEATUREDRAFT_H
