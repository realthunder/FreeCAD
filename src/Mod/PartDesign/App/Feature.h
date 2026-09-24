/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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


#ifndef PARTDESIGN_Feature_H
#define PARTDESIGN_Feature_H

#include <App/PropertyStandard.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

class gp_Pnt;
class gp_Pln;


/// Base class of all additive features in PartDesign
namespace PartDesign
{

using TopoShape = Part::TopoShape;

class Body;

 /** PartDesign feature
 *   Base class of all PartDesign features.
 *   This kind of features only produce solids or fail.
 */
class PartDesignExport Feature : public Part::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Feature);

public:
    Feature();

    /// Base feature which this feature will be fused into or cut out of
    App::PropertyLink   BaseFeature;
    mutable App::PropertyLinkHidden _Body;
    App::PropertyBool Suppress;
    Part::PropertyPartShape SuppressedShape;
    App::PropertyBool NewSolid;
    App::PropertyLinkList _Siblings;

    short mustExecute() const override;

    /// Check whether the given feature is a datum feature
    static bool isDatum(const App::DocumentObject* feature);

    /// Returns the body the feature is in, or none
    Body* getFeatureBody() const;

    /**
     * Returns the BaseFeature property's object (if any)
     * @param silent if couldn't determine the base feature and silent == true,
     *               silently return a nullptr, otherwise throw Base::Exception.
     *               Default is false.
     */
    virtual Part::Feature* getBaseObject(bool silent=false) const;
    /// Returns the BaseFeature property's shape (if any)
    virtual TopoShape getBaseShape(bool silent=false, bool force=false, bool checkSolid=true) const;
    const TopoDS_Shape& getBaseShapeOld() const;

    PyObject* getPyObject() override;

    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProvider";
    }

    virtual bool isElementGenerated(const TopoShape &shape, const Data::MappedName &name) const;

    virtual void getGeneratedIndices(std::vector<int> &faces,
                                     std::vector<int> &edges,
                                     std::vector<int> &vertices) const;

    void onChanged(const App::Property* prop) override;

    App::DocumentObject *getSubObject(const char *subname, 
        PyObject **pyObj, Base::Matrix4D *pmat, bool transform, int depth) const override;

    TopoShape getSolid(const TopoShape &, bool force = true);

    /** Called by Body::removeObject() just before this feature's BaseFeature
     *  is rerouted from \a oldBase, which is being removed, to \a newBase.
     *  \a oldBase still has its shape. Overrides relink the element
     *  references that follow the base feature.
     */
    virtual void onBaseFeatureRerouted(App::DocumentObject* oldBase,
                                       App::DocumentObject* newBase);

protected:
    /** Relinks \a link from \a oldBase to \a newBase when every element it
     *  names is found by geometry in \a newBase exactly once. Otherwise
     *  leaves it alone and returns false.
     */
    static bool relinkToMatchingSubElements(App::PropertyLinkSub& link,
                                            App::DocumentObject* oldBase,
                                            App::DocumentObject* newBase);


    App::DocumentObjectExecReturn *recompute() override;

    virtual void onNewSolidChanged();

    /**
     * Get a solid of the given shape. If no solid is found an exception is raised.
     */
    bool allowMultiSolid() const;

    void updateSuppressedShape();

    /** Returns \a shape, given in this feature's local frame, ready for
     *  Shape.setValue(). That call makes the shape's own location and
     *  Placement one and the same, so a shape that still carries a location
     *  -- a base shape a boolean handed back untouched -- is wrapped in a
     *  compound, which takes Placement while the shape keeps its offset.
     */
    TopoShape wrapLocated(const TopoShape& shape) const;

    /// The base shape, placed by this feature's Placement, for a suppressed feature
    TopoShape getPlacedBaseShape() const;

    /// Grab any point from the given face
    static const gp_Pnt getPointFromFace(const TopoDS_Face& f);
    /// Make a shape from a base plane (convenience method)
    static gp_Pln makePlnFromPlane(const App::DocumentObject* obj);
    static TopoShape makeShapeFromPlane(const App::DocumentObject* obj);
};

using FeaturePython = App::FeaturePythonT<Feature>;

} //namespace PartDesign


#endif // PARTDESIGN_Feature_H
