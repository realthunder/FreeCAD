/****************************************************************************
 *   Copyright (c) 2017 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef APP_LINK_H
#define APP_LINK_H

#include <unordered_set>
#include <Base/Parameter.h>
#include <Base/Bitmask.h>
#include "DocumentObject.h"
#include "DocumentObjectExtension.h"
#include "FeaturePython.h"
#include "GroupExtension.h"
#include "LinkParams.h"
#include "PropertyLinks.h"

#define LINK_THROW(_type,_msg) do{\
    if(FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))\
        FC_ERR(_msg);\
    throw _type(_msg);\
}while(0)

namespace App
{

class AppExport LinkBaseExtension : public App::DocumentObjectExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(App::LinkExtension);
    using inherited = App::DocumentObjectExtension;

public:
    LinkBaseExtension();
    ~LinkBaseExtension() override = default;

    PropertyBool _LinkTouched;
    PropertyInteger _LinkVersion;
    PropertyInteger _LinkOwner;
    PropertyLinkList _ChildCache; // cache for plain group expansion

    enum {
        LinkModeNone,
        LinkModeAutoDelete,
        LinkModeAutoLink,
        LinkModeAutoUnlink,
    };

    virtual void setProperty(int idx, Property *prop);
    Property *getProperty(int idx);
    Property *getProperty(const char *);

    struct PropInfo {
        int index;
        const char *name;
        Base::Type type;
        const char *doc;

        PropInfo(int index, const char *name,Base::Type type,const char *doc)
            : index(index), name(name), type(type), doc(doc)
        {}

        PropInfo() : index(0), name(0), doc(0) {}
    };

    enum LinkCopyOnChangeType {
        CopyOnChangeDisabled = 0,
        CopyOnChangeEnabled = 1,
        CopyOnChangeOwned = 2,
        CopyOnChangeTracking = 3
    };

    /*[[[cog
    import Link
    Link.declare_link_base_extension()
    ]]]*/

    // Auto generated code (App/Link.py:176)
    /// Indices for predefined properties
    enum PropIndex {
        PropLinkPlacement = 0,
        PropPlacement = 1,
        PropLinkedObject = 2,
        PropLinkTransform = 3,
        PropLinkClaimChild = 4,
        PropLinkCopyOnChange = 5,
        PropLinkCopyOnChangeSource = 6,
        PropLinkCopyOnChangeGroup = 7,
        PropLinkCopyOnChangeTouched = 8,
        PropSyncGroupVisibility = 9,
        PropScale = 10,
        PropScaleVector = 11,
        PropMatrix = 12,
        PropPlacementList = 13,
        PropAutoPlacement = 14,
        PropScaleList = 15,
        PropMatrixList = 16,
        PropVisibilityList = 17,
        PropElementCount = 18,
        PropElementList = 19,
        PropShowElement = 20,
        PropAutoLinkLabel = 21,
        PropLinkMode = 22,
        PropLinkExecute = 23,
        PropColoredElements = 24,
        PropMax
    };

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkPlacement
    Base::Placement getLinkPlacementValue() const {
        if (auto prop = this->props[PropIndex::PropLinkPlacement])
            return static_cast<const App::PropertyPlacement *>(prop)->getValue();
        return Base::Placement{};
    }
    const App::PropertyPlacement *getLinkPlacementProperty() const {
        return static_cast<const App::PropertyPlacement *>(this->props[PropIndex::PropLinkPlacement]);
    }
    App::PropertyPlacement *getLinkPlacementProperty() {
        return static_cast<App::PropertyPlacement *>(this->props[PropIndex::PropLinkPlacement]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property Placement
    Base::Placement getPlacementValue() const {
        if (auto prop = this->props[PropIndex::PropPlacement])
            return static_cast<const App::PropertyPlacement *>(prop)->getValue();
        return Base::Placement{};
    }
    const App::PropertyPlacement *getPlacementProperty() const {
        return static_cast<const App::PropertyPlacement *>(this->props[PropIndex::PropPlacement]);
    }
    App::PropertyPlacement *getPlacementProperty() {
        return static_cast<App::PropertyPlacement *>(this->props[PropIndex::PropPlacement]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkedObject
    App::DocumentObject* getLinkedObjectValue() const {
        if (auto prop = this->props[PropIndex::PropLinkedObject])
            return static_cast<const App::PropertyLink *>(prop)->getValue();
        return nullptr;
    }
    const App::PropertyLink *getLinkedObjectProperty() const {
        return static_cast<const App::PropertyLink *>(this->props[PropIndex::PropLinkedObject]);
    }
    App::PropertyLink *getLinkedObjectProperty() {
        return static_cast<App::PropertyLink *>(this->props[PropIndex::PropLinkedObject]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkTransform
    bool getLinkTransformValue() const {
        if (auto prop = this->props[PropIndex::PropLinkTransform])
            return static_cast<const App::PropertyBool *>(prop)->getValue();
        return false;
    }
    const App::PropertyBool *getLinkTransformProperty() const {
        return static_cast<const App::PropertyBool *>(this->props[PropIndex::PropLinkTransform]);
    }
    App::PropertyBool *getLinkTransformProperty() {
        return static_cast<App::PropertyBool *>(this->props[PropIndex::PropLinkTransform]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkClaimChild
    bool getLinkClaimChildValue() const {
        if (auto prop = this->props[PropIndex::PropLinkClaimChild])
            return static_cast<const App::PropertyBool *>(prop)->getValue();
        return false;
    }
    const App::PropertyBool *getLinkClaimChildProperty() const {
        return static_cast<const App::PropertyBool *>(this->props[PropIndex::PropLinkClaimChild]);
    }
    App::PropertyBool *getLinkClaimChildProperty() {
        return static_cast<App::PropertyBool *>(this->props[PropIndex::PropLinkClaimChild]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkCopyOnChange
    long getLinkCopyOnChangeValue() const {
        if (auto prop = this->props[PropIndex::PropLinkCopyOnChange])
            return static_cast<const App::PropertyEnumeration *>(prop)->getValue();
        return long(0);
    }
    const App::PropertyEnumeration *getLinkCopyOnChangeProperty() const {
        return static_cast<const App::PropertyEnumeration *>(this->props[PropIndex::PropLinkCopyOnChange]);
    }
    App::PropertyEnumeration *getLinkCopyOnChangeProperty() {
        return static_cast<App::PropertyEnumeration *>(this->props[PropIndex::PropLinkCopyOnChange]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkCopyOnChangeSource
    App::DocumentObject* getLinkCopyOnChangeSourceValue() const {
        if (auto prop = this->props[PropIndex::PropLinkCopyOnChangeSource])
            return static_cast<const App::PropertyLink *>(prop)->getValue();
        return nullptr;
    }
    const App::PropertyLink *getLinkCopyOnChangeSourceProperty() const {
        return static_cast<const App::PropertyLink *>(this->props[PropIndex::PropLinkCopyOnChangeSource]);
    }
    App::PropertyLink *getLinkCopyOnChangeSourceProperty() {
        return static_cast<App::PropertyLink *>(this->props[PropIndex::PropLinkCopyOnChangeSource]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkCopyOnChangeGroup
    App::DocumentObject* getLinkCopyOnChangeGroupValue() const {
        if (auto prop = this->props[PropIndex::PropLinkCopyOnChangeGroup])
            return static_cast<const App::PropertyLink *>(prop)->getValue();
        return nullptr;
    }
    const App::PropertyLink *getLinkCopyOnChangeGroupProperty() const {
        return static_cast<const App::PropertyLink *>(this->props[PropIndex::PropLinkCopyOnChangeGroup]);
    }
    App::PropertyLink *getLinkCopyOnChangeGroupProperty() {
        return static_cast<App::PropertyLink *>(this->props[PropIndex::PropLinkCopyOnChangeGroup]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkCopyOnChangeTouched
    bool getLinkCopyOnChangeTouchedValue() const {
        if (auto prop = this->props[PropIndex::PropLinkCopyOnChangeTouched])
            return static_cast<const App::PropertyBool *>(prop)->getValue();
        return false;
    }
    const App::PropertyBool *getLinkCopyOnChangeTouchedProperty() const {
        return static_cast<const App::PropertyBool *>(this->props[PropIndex::PropLinkCopyOnChangeTouched]);
    }
    App::PropertyBool *getLinkCopyOnChangeTouchedProperty() {
        return static_cast<App::PropertyBool *>(this->props[PropIndex::PropLinkCopyOnChangeTouched]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property SyncGroupVisibility
    bool getSyncGroupVisibilityValue() const {
        if (auto prop = this->props[PropIndex::PropSyncGroupVisibility])
            return static_cast<const App::PropertyBool *>(prop)->getValue();
        return false;
    }
    const App::PropertyBool *getSyncGroupVisibilityProperty() const {
        return static_cast<const App::PropertyBool *>(this->props[PropIndex::PropSyncGroupVisibility]);
    }
    App::PropertyBool *getSyncGroupVisibilityProperty() {
        return static_cast<App::PropertyBool *>(this->props[PropIndex::PropSyncGroupVisibility]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property Scale
    double getScaleValue() const {
        if (auto prop = this->props[PropIndex::PropScale])
            return static_cast<const App::PropertyFloat *>(prop)->getValue();
        return 1.0;
    }
    const App::PropertyFloat *getScaleProperty() const {
        return static_cast<const App::PropertyFloat *>(this->props[PropIndex::PropScale]);
    }
    App::PropertyFloat *getScaleProperty() {
        return static_cast<App::PropertyFloat *>(this->props[PropIndex::PropScale]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property ScaleVector
    Base::Vector3d getScaleVectorValue() const {
        if (auto prop = this->props[PropIndex::PropScaleVector])
            return static_cast<const App::PropertyVector *>(prop)->getValue();
        return Base::Vector3d(1.0, 1.0 ,1.0);
    }
    const App::PropertyVector *getScaleVectorProperty() const {
        return static_cast<const App::PropertyVector *>(this->props[PropIndex::PropScaleVector]);
    }
    App::PropertyVector *getScaleVectorProperty() {
        return static_cast<App::PropertyVector *>(this->props[PropIndex::PropScaleVector]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property Matrix
    Base::Matrix4D getMatrixValue() const {
        if (auto prop = this->props[PropIndex::PropMatrix])
            return static_cast<const App::PropertyMatrix *>(prop)->getValue();
        return Base::Matrix4D{};
    }
    const App::PropertyMatrix *getMatrixProperty() const {
        return static_cast<const App::PropertyMatrix *>(this->props[PropIndex::PropMatrix]);
    }
    App::PropertyMatrix *getMatrixProperty() {
        return static_cast<App::PropertyMatrix *>(this->props[PropIndex::PropMatrix]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property PlacementList
    std::vector<Base::Placement> getPlacementListValue() const {
        if (auto prop = this->props[PropIndex::PropPlacementList])
            return static_cast<const App::PropertyPlacementList *>(prop)->getValue();
        return std::vector<Base::Placement>{};
    }
    const App::PropertyPlacementList *getPlacementListProperty() const {
        return static_cast<const App::PropertyPlacementList *>(this->props[PropIndex::PropPlacementList]);
    }
    App::PropertyPlacementList *getPlacementListProperty() {
        return static_cast<App::PropertyPlacementList *>(this->props[PropIndex::PropPlacementList]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property AutoPlacement
    bool getAutoPlacementValue() const {
        if (auto prop = this->props[PropIndex::PropAutoPlacement])
            return static_cast<const App::PropertyBool *>(prop)->getValue();
        return true;
    }
    const App::PropertyBool *getAutoPlacementProperty() const {
        return static_cast<const App::PropertyBool *>(this->props[PropIndex::PropAutoPlacement]);
    }
    App::PropertyBool *getAutoPlacementProperty() {
        return static_cast<App::PropertyBool *>(this->props[PropIndex::PropAutoPlacement]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property ScaleList
    std::vector<Base::Vector3d> getScaleListValue() const {
        if (auto prop = this->props[PropIndex::PropScaleList])
            return static_cast<const App::PropertyVectorList *>(prop)->getValue();
        return std::vector<Base::Vector3d>{};
    }
    const App::PropertyVectorList *getScaleListProperty() const {
        return static_cast<const App::PropertyVectorList *>(this->props[PropIndex::PropScaleList]);
    }
    App::PropertyVectorList *getScaleListProperty() {
        return static_cast<App::PropertyVectorList *>(this->props[PropIndex::PropScaleList]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property MatrixList
    std::vector<Base::Matrix4D> getMatrixListValue() const {
        if (auto prop = this->props[PropIndex::PropMatrixList])
            return static_cast<const App::PropertyMatrixList *>(prop)->getValue();
        return std::vector<Base::Matrix4D>{};
    }
    const App::PropertyMatrixList *getMatrixListProperty() const {
        return static_cast<const App::PropertyMatrixList *>(this->props[PropIndex::PropMatrixList]);
    }
    App::PropertyMatrixList *getMatrixListProperty() {
        return static_cast<App::PropertyMatrixList *>(this->props[PropIndex::PropMatrixList]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property VisibilityList
    boost::dynamic_bitset<> getVisibilityListValue() const {
        if (auto prop = this->props[PropIndex::PropVisibilityList])
            return static_cast<const App::PropertyBoolList *>(prop)->getValue();
        return boost::dynamic_bitset<>{};
    }
    const App::PropertyBoolList *getVisibilityListProperty() const {
        return static_cast<const App::PropertyBoolList *>(this->props[PropIndex::PropVisibilityList]);
    }
    App::PropertyBoolList *getVisibilityListProperty() {
        return static_cast<App::PropertyBoolList *>(this->props[PropIndex::PropVisibilityList]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property ElementCount
    int getElementCountValue() const {
        if (auto prop = this->props[PropIndex::PropElementCount])
            return static_cast<const App::PropertyInteger *>(prop)->getValue();
        return 0;
    }
    const App::PropertyInteger *getElementCountProperty() const {
        return static_cast<const App::PropertyInteger *>(this->props[PropIndex::PropElementCount]);
    }
    App::PropertyInteger *getElementCountProperty() {
        return static_cast<App::PropertyInteger *>(this->props[PropIndex::PropElementCount]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property ElementList
    std::vector<App::DocumentObject*> getElementListValue() const {
        if (auto prop = this->props[PropIndex::PropElementList])
            return static_cast<const App::PropertyLinkList *>(prop)->getValue();
        return std::vector<App::DocumentObject*>{};
    }
    const App::PropertyLinkList *getElementListProperty() const {
        return static_cast<const App::PropertyLinkList *>(this->props[PropIndex::PropElementList]);
    }
    App::PropertyLinkList *getElementListProperty() {
        return static_cast<App::PropertyLinkList *>(this->props[PropIndex::PropElementList]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property ShowElement
    bool getShowElementValue() const {
        if (auto prop = this->props[PropIndex::PropShowElement])
            return static_cast<const App::PropertyBool *>(prop)->getValue();
        return true;
    }
    const App::PropertyBool *getShowElementProperty() const {
        return static_cast<const App::PropertyBool *>(this->props[PropIndex::PropShowElement]);
    }
    App::PropertyBool *getShowElementProperty() {
        return static_cast<App::PropertyBool *>(this->props[PropIndex::PropShowElement]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property AutoLinkLabel
    bool getAutoLinkLabelValue() const {
        if (auto prop = this->props[PropIndex::PropAutoLinkLabel])
            return static_cast<const App::PropertyBool *>(prop)->getValue();
        return false;
    }
    const App::PropertyBool *getAutoLinkLabelProperty() const {
        return static_cast<const App::PropertyBool *>(this->props[PropIndex::PropAutoLinkLabel]);
    }
    App::PropertyBool *getAutoLinkLabelProperty() {
        return static_cast<App::PropertyBool *>(this->props[PropIndex::PropAutoLinkLabel]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkMode
    long getLinkModeValue() const {
        if (auto prop = this->props[PropIndex::PropLinkMode])
            return static_cast<const App::PropertyEnumeration *>(prop)->getValue();
        return long(0);
    }
    const App::PropertyEnumeration *getLinkModeProperty() const {
        return static_cast<const App::PropertyEnumeration *>(this->props[PropIndex::PropLinkMode]);
    }
    App::PropertyEnumeration *getLinkModeProperty() {
        return static_cast<App::PropertyEnumeration *>(this->props[PropIndex::PropLinkMode]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property LinkExecute
    const char* getLinkExecuteValue() const {
        if (auto prop = this->props[PropIndex::PropLinkExecute])
            return static_cast<const App::PropertyString *>(prop)->getValue();
        return "";
    }
    const App::PropertyString *getLinkExecuteProperty() const {
        return static_cast<const App::PropertyString *>(this->props[PropIndex::PropLinkExecute]);
    }
    App::PropertyString *getLinkExecuteProperty() {
        return static_cast<App::PropertyString *>(this->props[PropIndex::PropLinkExecute]);
    }
    //@}

    // Auto generated code (App/Link.py:68)
    //@{
    /// Accessor for property ColoredElements
    App::DocumentObject* getColoredElementsValue() const {
        if (auto prop = this->props[PropIndex::PropColoredElements])
            return static_cast<const App::PropertyLinkSubHidden *>(prop)->getValue();
        return nullptr;
    }
    const App::PropertyLinkSubHidden *getColoredElementsProperty() const {
        return static_cast<const App::PropertyLinkSubHidden *>(this->props[PropIndex::PropColoredElements]);
    }
    App::PropertyLinkSubHidden *getColoredElementsProperty() {
        return static_cast<App::PropertyLinkSubHidden *>(this->props[PropIndex::PropColoredElements]);
    }
    //@}

    // Auto generated code (App/Link.py:190)
    static const std::vector<PropInfo> &getPropertyInfo();
    //[[[end]]]

    using PropInfoMap = std::map<std::string, PropInfo>;
    static const PropInfoMap &getPropertyInfoMap();

    PropertyLinkList *_getElementListProperty() const;
    const std::vector<App::DocumentObject*> &_getElementListValue() const;

    PropertyBool *_getShowElementProperty() const;
    bool _getShowElementValue() const;

    PropertyInteger *_getElementCountProperty() const;
    int _getElementCountValue() const;

    std::vector<DocumentObject*> getLinkedChildren(bool filter=true) const;

    const char *flattenSubname(const char *subname) const;
    void expandSubname(std::string &subname) const;

    DocumentObject *getLink(int depth=0) const;

    Base::Matrix4D getTransform(bool transform) const;
    Base::Vector3d getScaleVector() const;

    App::GroupExtension *linkedPlainGroup() const;

    bool linkTransform() const;

    const char *getSubName() const {
        parseSubName();
        return !mySubName.empty()?mySubName.c_str():nullptr;
    }

    const std::vector<std::string> &getSubElements() const {
        parseSubName();
        return mySubElements;
    }

    bool extensionGetSubObject(DocumentObject *&ret, const char *subname,
            PyObject **pyObj=nullptr, Base::Matrix4D *mat=nullptr, bool transform=false, int depth=0) const override;

    bool extensionGetSubObjects(std::vector<std::string>&ret, int reason) const override;

    bool extensionGetLinkedObject(DocumentObject *&ret,
            bool recurse, Base::Matrix4D *mat, bool transform, int depth) const override;

    App::DocumentObjectExecReturn *extensionExecute(void) override;
    short extensionMustExecute(void) override;
    void extensionOnChanged(const Property* p) override;
    void onExtendedSetupObject () override;
    void onExtendedUnsetupObject () override;
    void onExtendedDocumentRestored() override;

    int extensionSetElementVisible(const char *, bool) override;
    int extensionIsElementVisible(const char *) const override;
    int extensionIsElementVisibleEx(const char *,int) const override;
    bool extensionHasChildElement() const override;

    PyObject* getExtensionPyObject() override;

    Property *extensionGetPropertyByName(const char* name) const override;
    void extensionGetPropertyNamedList(std::vector<std::pair<const char *,Property*> > &) const override;

    static int getArrayIndex(const char *subname, const char **psubname=nullptr);
    int getElementIndex(const char *subname, const char **psubname=nullptr) const;
    void elementNameFromIndex(int idx, std::ostream &ss) const;

    static std::vector<std::string> getHiddenSubnames(
            const App::DocumentObject *obj, const char *prefix=0);

    static bool isSubnameHidden(const App::DocumentObject *obj, const char *subname);

    DocumentObject *getContainer();
    const DocumentObject *getContainer() const;

    void setLink(int index, DocumentObject *obj, const char *subname=nullptr,
        const std::vector<std::string> &subs = std::vector<std::string>());

    DocumentObject *getTrueLinkedObject(bool recurse,
            Base::Matrix4D *mat=nullptr,int depth=0, bool noElement=false) const;

    using LinkPropMap = std::map<const Property*,std::pair<LinkBaseExtension*,int> >;

    bool hasPlacement() const {
        return getLinkPlacementProperty() || getPlacementProperty();
    }

    void cacheChildLabel(int enable=-1) const;

    static bool setupCopyOnChange(App::DocumentObject *obj, App::DocumentObject *linked,
            std::vector<boost::signals2::scoped_connection> *copyOnChangeConns, bool checkExisting);

    static bool isCopyOnChangeProperty(App::DocumentObject *obj, const Property &prop);

    boost::signals2::signal<
        void (App::DocumentObject & /*parent*/,
              int /*startIndex*/,
              int /*endIndex*/,
              std::vector<App::DocumentObject *> * /*elements, maybe null if not showing elements*/)
        > signalNewLinkElements;

    void syncCopyOnChange();

    /** Options used in setOnChangeCopyObject()
     * Multiple options can be combined by bitwise or operator
     */
    enum class OnChangeCopyOptions {
        /// No options set
        None = 0,
        /// If set, then exclude the input from object list to copy on change, or else, include the input object.
        Exclude = 1,
        /// If set , then apply the setting to all links to the input object, or else, apply only to this link.
        ApplyAll = 2,
    };

    /** Include or exclude object from list of objects to copy on change
     * @param obj: input object
     * @param options: control options. @sa OnChangeCopyOptions.
     */
    void setOnChangeCopyObject(App::DocumentObject *obj, OnChangeCopyOptions options);

    std::vector<App::DocumentObject *> getOnChangeCopyObjects(
            std::vector<App::DocumentObject *> *excludes = nullptr,
            App::DocumentObject *src = nullptr);

    bool isLinkedToConfigurableObject() const;

    void monitorOnChangeCopyObjects(const std::vector<App::DocumentObject*> &objs);

    /// Check if the linked object is a copy on change
    bool isLinkMutated() const;

protected:
    void _handleChangedPropertyName(Base::XMLReader &reader,
            const char * TypeName, const char *PropName);
    void parseSubName() const;
    void update(App::DocumentObject *parent, const Property *prop);
    void checkCopyOnChange(App::DocumentObject *parent, const App::Property &prop);
    void setupCopyOnChange(App::DocumentObject *parent, bool checkSource = false);
    App::DocumentObject *makeCopyOnChange();
    void syncElementList();
    void detachElement(App::DocumentObject *obj);
    void detachElements();
    void checkGeoElementMap(const App::DocumentObject *obj,
        const App::DocumentObject *linked, PyObject **pyObj, const char *postfix) const;
    void updateGroup();
    void updateGroupVisibility();
    void slotLabelChanged();

protected:
    std::vector<Property *> props;
    std::unordered_set<const App::DocumentObject*> myHiddenElements;
    mutable std::vector<std::string> mySubElements;
    mutable std::string mySubName;

    std::vector<boost::signals2::scoped_connection> plainGroupConns;

    long prevLinkedObjectID = 0;

    mutable std::unordered_map<std::string,int> myLabelCache; // for label based subname lookup
    mutable bool enableLabelCache{false};
    bool hasOldSubElement{false};

    std::vector<boost::signals2::scoped_connection> copyOnChangeConns;
    std::vector<boost::signals2::scoped_connection> copyOnChangeSrcConns;
    bool hasCopyOnChange{true};

    mutable bool checkingProperty = false;
    bool pauseCopyOnChange = false;

    boost::signals2::scoped_connection connLabelChange;
    boost::signals2::scoped_connection connCopyOnChangeSource;
};

///////////////////////////////////////////////////////////////////////////

using LinkBaseExtensionPython = ExtensionPythonT<LinkBaseExtension>;

///////////////////////////////////////////////////////////////////////////

class AppExport LinkExtension : public LinkBaseExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(App::LinkExtension);
    using inherited = LinkBaseExtension;

public:
    LinkExtension();
    ~LinkExtension() override = default;

    /*[[[cog
    import Link
    Link.declare_link_extension()
    ]]]*/

    // Auto generated code (App/Link.py:226)
    App::PropertyFloat Scale;
    App::PropertyVector ScaleVector;
    App::PropertyMatrix Matrix;
    App::PropertyVectorList ScaleList;
    App::PropertyMatrixList MatrixList;
    App::PropertyBoolList VisibilityList;
    App::PropertyPlacementList PlacementList;
    App::PropertyBool AutoPlacement;
    App::PropertyLinkList ElementList;

    // Auto generated code (App/Link.py:233)
    void registerProperties();
    void onExtendedDocumentRestored() override;
    //[[[end]]]
};

///////////////////////////////////////////////////////////////////////////

using LinkExtensionPython = ExtensionPythonT<LinkExtension>;

///////////////////////////////////////////////////////////////////////////

class AppExport Link : public App::DocumentObject, public App::LinkExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(App::Link);
    using inherited = App::DocumentObject;
    using inherited_extension = App::LinkExtension;
public:
    Link();

    /*[[[cog
    import Link
    Link.declare_link()
    ]]]*/

    // Auto generated code (App/Link.py:269)
    App::PropertyXLink LinkedObject;
    App::PropertyBool LinkClaimChild;
    App::PropertyBool LinkTransform;
    App::PropertyPlacement LinkPlacement;
    App::PropertyPlacement Placement;
    App::PropertyBool ShowElement;
    App::PropertyBool SyncGroupVisibility;
    App::PropertyIntegerConstraint ElementCount;
    App::PropertyString LinkExecute;
    App::PropertyLinkSubHidden ColoredElements;
    App::PropertyEnumeration LinkCopyOnChange;
    App::PropertyXLink LinkCopyOnChangeSource;
    App::PropertyLink LinkCopyOnChangeGroup;
    App::PropertyBool LinkCopyOnChangeTouched;
    App::PropertyBool AutoLinkLabel;

    // Auto generated code (App/Link.py:276)
    void registerProperties();
    void onDocumentRestored() override;
    //[[[end]]]

    const char* getViewProviderName() const override{
        return "Gui::ViewProviderLink";
    }

    void handleChangedPropertyName(Base::XMLReader &reader,
            const char * TypeName, const char *PropName) override
    {
        _handleChangedPropertyName(reader,TypeName,PropName);
    }

    bool canLinkProperties() const override;

    void setupObject() override;
};

using LinkPython = App::FeaturePythonT<Link>;

///////////////////////////////////////////////////////////////////////////

class AppExport LinkElement : public App::DocumentObject, public App::LinkBaseExtension {
    PROPERTY_HEADER_WITH_EXTENSIONS(App::LinkElement);
    using inherited = App::DocumentObject;
    using inherited_extension =  App::LinkBaseExtension;
public:
    LinkElement();

    /*[[[cog
    import Link
    Link.declare_link_element()
    ]]]*/

    // Auto generated code (App/Link.py:269)
    App::PropertyFloat Scale;
    App::PropertyVector ScaleVector;
    App::PropertyMatrix Matrix;
    App::PropertyXLink LinkedObject;
    App::PropertyBool LinkClaimChild;
    App::PropertyBool LinkTransform;
    App::PropertyPlacement LinkPlacement;
    App::PropertyPlacement Placement;
    App::PropertyEnumeration LinkCopyOnChange;
    App::PropertyXLink LinkCopyOnChangeSource;
    App::PropertyLink LinkCopyOnChangeGroup;
    App::PropertyBool LinkCopyOnChangeTouched;

    // Auto generated code (App/Link.py:276)
    void registerProperties();
    void onDocumentRestored() override;
    //[[[end]]]

    const char* getViewProviderName() const override{
        return "Gui::ViewProviderLink";
    }
    bool canDelete() const;

    void handleChangedPropertyName(Base::XMLReader &reader,
            const char * TypeName, const char *PropName) override
    {
        _handleChangedPropertyName(reader,TypeName,PropName);
    }
};

using LinkElementPython = App::FeaturePythonT<LinkElement>;

///////////////////////////////////////////////////////////////////////////

class AppExport LinkGroup : public App::DocumentObject, public App::LinkBaseExtension {
    PROPERTY_HEADER_WITH_EXTENSIONS(App::LinkGroup);
    using inherited = App::DocumentObject;
    using inherited_extension = App::LinkBaseExtension;
public:
    LinkGroup();

    /*[[[cog
    import Link
    Link.declare_link_group()
    ]]]*/

    // Auto generated code (App/Link.py:269)
    App::PropertyLinkList ElementList;
    App::PropertyPlacement Placement;
    App::PropertyBoolList VisibilityList;
    App::PropertyEnumeration LinkMode;
    App::PropertyLinkSubHidden ColoredElements;

    // Auto generated code (App/Link.py:276)
    void registerProperties();
    void onDocumentRestored() override;
    //[[[end]]]

    const char* getViewProviderName() const override{
        return "Gui::ViewProviderLink";
    }
};

using LinkGroupPython = App::FeaturePythonT<LinkGroup>;

} //namespace App

ENABLE_BITMASK_OPERATORS(App::Link::OnChangeCopyOptions)

#endif // APP_LINK_H
