/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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


#ifndef PARTDESIGN_Revolved_H
#define PARTDESIGN_Revolved_H

#include <App/PropertyUnits.h>
#include "FeatureSketchBased.h"

class gp_Ax1;

namespace PartDesign
{

/** What Revolution and Groove share (upstream 0e6494c35f): the axis, the
 * sides (upstream b2da06bfe0) and the angular start (upstream 06a4b1db99).
 *
 * The subclasses add the properties, under their own groups and with their
 * own Type enums, as upstream does.
 */
class PartDesignExport Revolved : public ProfileBased
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Revolved);

public:
    Revolved();

    /// "One side", "Two sides" or "Symmetric". The ProfileBased Midplane
    /// stays as an alias of Symmetric, and Type "TwoAngles" becomes Two
    /// sides with both sides Angle, as FeatureExtrude does with TwoLengths.
    App::PropertyEnumeration SideType;
    App::PropertyEnumeration Type;
    /// The type of the second side, for SideType "Two sides"
    App::PropertyEnumeration Type2;
    App::PropertyVector      Base;
    App::PropertyVector      Axis;
    App::PropertyAngle       Angle;
    App::PropertyAngle       Angle2;
    /// The face the second side ends at
    App::PropertyLinkSub     UpToFace2;
    /// Where the revolution starts: see ProfileBased::StartTypesEnums
    App::PropertyEnumeration StartType;
    App::PropertyAngle       StartOffset;
    App::PropertyLinkSub     StartReference;

    /** if this property is set to a valid link, both Axis and Base properties
     *  are calculated according to the linked line
    */
    App::PropertyLinkSub ReferenceAxis;

    static const char* SideTypeEnums[];

    /** @name methods override feature */
    //@{
    short mustExecute() const override;
    //@}

    /// suggests a value for Reversed flag so that material is always added
    /// to (Revolution) or removed from (Groove) the support
    bool suggestReversed();

    /** How far, in degrees, the profile turns to start where StartType
     * says, about the axis the last recompute stated; throws if the
     * reference cannot be met. For the panel and its gizmo.
     */
    double getStartOffset() const;

    /// The Type values by index; both subclasses keep them there
    enum class RevolMethod {
        Angle,
        ThroughAll,
        ToLast = ThroughAll,
        ToFirst,
        ToFace,
        TwoAngles
    };

    /// The Type (or Type2) value, TwoAngles read as Angle
    static RevolMethod methodOf(const App::PropertyEnumeration &type);

protected:
    App::DocumentObjectExecReturn *executeRevolved();

    void onChanged(const App::Property* prop) override;
    void onDocumentRestored() override;

    /// Whether Reversed gives material on the support side, for the signed
    /// angle between the axis and the profile normal
    virtual bool suggestReversedAngle(double angle) const = 0;
    /// Whether this is a Groove: its second Type is ThroughAll (a
    /// Revolution's is UpToLast), it needs a base and a closed profile.
    /// The Operation (AddSubType) may still be switched in the panel.
    virtual bool isGroove() const = 0;

    /// updates Axis from ReferenceAxis
    void updateAxis();

private:
    /// A side revolved by angle from sketchshape, as the old features made it
    void generateRevolution(TopoShape& revol,
                            const TopoShape& sketchshape,
                            const gp_Ax1& axis,
                            double angle,
                            double angle2,
                            bool midplane,
                            RevolMethod method) const;

    /// See BRepFeat_MakeRevol
    enum class BRepFeatMode {
        CutFromBase = 0,
        FuseWithBase = 1,
        None = 2
    };

    /** A side revolved up to uptoface with BRepFeat_MakeRevol. Mode None
     * fuses the faces after the first; for the first it gives the base with
     * the side fused in too, not the side alone
     */
    void generateRevolution(TopoShape& revol,
                            const TopoShape& baseshape,
                            const TopoShape& profileshape,
                            const TopoShape& supportface,
                            const TopoShape& uptoface,
                            const gp_Ax1& axis,
                            BRepFeatMode mode) const;

    /** The side revolved up to upToFace, alone. A Revolution's is the base
     * with the side fused in, less the base; the former goes to baseResult
     */
    TopoShape revolveUpTo(const TopoShape& base,
                          const TopoShape& sketchshape,
                          const TopoShape& supportface,
                          const TopoShape& upToFace,
                          const gp_Ax1& axis,
                          TopoShape* baseResult = nullptr) const;

    /// The face a side of method ends at
    TopoShape getRevolutionUpToFace(RevolMethod method,
                                    const App::PropertyLinkSub& upToFace,
                                    const TopoShape& base,
                                    const TopoShape& sketchshape,
                                    const TopLoc_Location& invObjLoc,
                                    const gp_Ax1& axis) const;

    /// One side, or a null shape for an angle of zero
    TopoShape makeSide(RevolMethod method,
                       double angle,
                       const App::PropertyLinkSub& upToFace,
                       const TopoShape& sketchshape,
                       const TopoShape& base,
                       const TopoShape& supportface,
                       const TopLoc_Location& invObjLoc,
                       const gp_Ax1& axis,
                       TopoShape* baseResult = nullptr) const;

    /// The start angle, in radians about axis, the profile turns to
    double startAngle(const TopoShape& profile, const gp_Ax1& axis,
                      const TopLoc_Location& invObjLoc) const;
    /// The angle from profileShape about axis to the reference, plus offset
    double getStartReferenceAngle(const TopoShape& profileShape,
                                  const gp_Ax1& axis,
                                  double offset,
                                  const TopLoc_Location& invObjLoc) const;
    /// The axis the start turns about: the revolution's, reversed with it
    /// except for a symmetric one
    gp_Ax1 startAxis(const gp_Ax1& revolutionAxis) const;

    /// Disables settings that are not valid for the current methods
    void updateProperties();

    /// The Operation adds: the up-to side is BRepFeat's fuse less the base
    bool isAdditive() const;

    static const App::PropertyAngle::Constraints floatAngle;

    bool syncingSides = false;
};

} //namespace PartDesign


#endif // PARTDESIGN_Revolved_H
