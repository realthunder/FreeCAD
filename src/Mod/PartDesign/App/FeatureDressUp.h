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


#ifndef PARTDESIGN_DressUp_H
#define PARTDESIGN_DressUp_H

#include <map>

#include "FeatureAddSub.h"

namespace PartDesign
{

class PartDesignExport DressUp : public PartDesign::FeatureAddSub
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::DressUp);

public:
    DressUp();

    /**
     * Base feature and it's subelements to which dressup operation will be applied to.
     * Unlike Feature::BaseFeature it includes Sublinks and set not only inside a body.
     * But for consistency if BaseFeature is nonzero this links to the same body as it.
     */
    App::PropertyLinkSub Base;
    App::PropertyBool SupportTransform;
    Part::PropertyPartShape   DressUpShape;
    /** The edges of the last recompute whose fillet or chamfer failed or
     * whose reference is gone, left out of the result (docs/HandsOnQueue.md
     * entry 143); not saved
     */
    App::PropertyStringList FailedEdges;
    /// The faces those edges' fillets or chamfers had when they last were
    /// made; not saved
    Part::PropertyPartShape FailedShape;

    short mustExecute() const override;
    /// updates the Placement property from the Placement of the BaseFeature
    void positionByBaseFeature();
    /**
     * Returns the BaseFeature property's object if it's set otherwise returns Base's
     * feature property object otherwise feature property's object (if any)
     * @param silent if couldn't determine the base feature and silent == true,
     *               silently return a nullptr, otherwise throw Base::Exception.
     *               Default is false.
     */
    Part::Feature* getBaseObject(bool silent=false) const override;
    /// extracts all edges from the subshapes (including face edges) and furthermore adds
    /// all C0 continuous edges to the vector
    /** The edges of Base in \a shape. A reference that is gone throws,
     * or, given \a missing, is added to it and left out (docs/HandsOnQueue.md
     * entry 143).
     */
    std::vector<TopoShape> getContinuousEdges(const TopoShape &shape,
                                              std::vector<std::string> *missing = nullptr);

    std::vector<TopoShape> getFaces(const TopoShape &shape);

    void getAddSubShape(std::vector<std::pair<Part::TopoShape, Type> > &shapes) override;

    static const std::string &addsubElementPrefix();

    void setPauseRecompute(bool) override {
    }

    void onBaseFeatureRerouted(App::DocumentObject* oldBase,
                               App::DocumentObject* newBase) override;

protected:
    void onChanged(const App::Property* prop) override;

    /** After a build that left out what failed: \a failedNames gets the
     * names of the edges left out -- \a missing, then the failed in
     * \a baseShape's names -- and FailedEdges and FailedShape are set; the
     * faces of the edges made are kept for when they fail later.
     */
    void setFailedEdges(const TopoShape &baseShape, const std::vector<TopoShape> &edges,
                        const Part::TopoShape::FilletPartial &partial,
                        const std::vector<std::string> &missing,
                        std::vector<std::string> &failedNames);
    /// FailedEdges set to \a names, FailedShape to their faces as last made
    void setFailedEdges(const std::vector<std::string> &names);
    /// The error of a result that left \a names out, nullptr for none
    static App::DocumentObjectExecReturn *failedEdgesError(const char *what,
                                                           const std::vector<std::string> &names);

private:
    /// The faces of each edge's dress-up as last made, by the edge's name
    std::map<std::string, TopoDS_Shape> madeFaces;
};

} //namespace PartDesign


#endif // PARTDESIGN_DressUp_H
