/***************************************************************************
 *   Copyright (c) 2006 Jürgen Riegel <juergen.riegel@web.de>              *
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



#ifndef APP_FEATUREPYTHON_H
#define APP_FEATUREPYTHON_H

#include <App/FeaturePythonHook.h>
#include <App/GeoFeature.h>
#include <App/PropertyLinks.h>
#include <App/PropertyPythonObject.h>


namespace App
{

class Property;

/** The App-side hooks of a scripted object's Proxy
 *
 * One short body per hook over PyHookImp::callHook; the hook table itself is
 * generated from FeaturePythonHooks.py (docs/ProxyChain.md sec 3).  ValueT and
 * init() come from the base.
 */
class AppExport FeaturePythonImp: public PyHookImp
{
public:
    explicit FeaturePythonImp(App::DocumentObject*);
    ~FeaturePythonImp() override;

    bool execute();
    bool mustExecute() const;
    bool skipRecompute();
    void onBeforeChange(const Property* prop);
    bool onBeforeChangeLabel(std::string &newLabel);
    void onChanged(const Property* prop);
    void onDocumentRestored();
    void unsetupObject();
    std::string getViewProviderName();
    void setupObject() {}
    PyObject *getPyObject();

    bool getSubObject(App::DocumentObject *&ret, const char *subname, PyObject **pyObj,
            Base::Matrix4D *mat, bool transform, int depth) const;

    bool getSubObjects(std::vector<std::string> &ret, int reason) const;

    bool getLinkedObject(App::DocumentObject *&ret, bool recurse,
                         Base::Matrix4D *mat, bool transform, int depth) const;

    ValueT canLinkProperties() const;

    ValueT allowDuplicateLabel() const;

    ValueT redirectSubName(std::ostringstream &ss,
                           App::DocumentObject *topParent,
                           App::DocumentObject *child) const;

    int canLoadPartial() const;

    /// return true to activate tree view group object handling
    ValueT hasChildElement() const;
    /// Get sub-element visibility
    int isElementVisible(const char *) const;
    /// Set sub-element visibility
    int setElementVisible(const char *, bool);
    /// Get sub-object/element visibility
    int isElementVisibleEx(const char *, int) const;

    void  getElementMapVersion(std::string &ver, const App::Property *prop, bool restored) const;

    bool editProperty(const char *propName);

protected:
    Py::Object hookSelf(int hook) const override;

private:
    App::DocumentObject* object;
};

/**
 * Generic Python feature class which allows to behave every DocumentObject
 * derived class as Python feature -- simply by subclassing.
 * @author Werner Mayer
 */
template <class FeatureT>
class FeaturePythonT : public FeatureT
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::FeaturePythonT<FeatureT>);

public:
    FeaturePythonT() {
        ADD_PROPERTY(Proxy,(Py::Object()));
        ADD_PROPERTY_TYPE(ProxyExp,(nullptr),"Base",App::Prop_None,
                "Objects extending this object's Proxy hooks.  Each is asked, in\n"
                "order and before the Proxy, for a method named after the hook --\n"
                "expExecute(obj), expOnChanged(obj, prop), ... -- and the first\n"
                "that answers stops the chain.  A linked object's own Proxy is\n"
                "asked first, then the object itself, so a spreadsheet whose\n"
                "alias cells are lambdas extends this object as readily as a\n"
                "scripted one does.  The links are dependencies: an extension is\n"
                "recomputed before what it extends, and a copy with dependencies\n"
                "takes it along.");
        // a linked object may live in another file
        ProxyExp.setScope(LinkScope::Global);
        ADD_PROPERTY_TYPE(ViewProxyExp,(nullptr),"Base",App::Prop_NoRecompute,
                "Objects extending this object's VIEW PROVIDER Proxy hooks.  Each\n"
                "is asked, in order and before the view Proxy, for a method named\n"
                "after the hook -- expViewGetIcon(vobj), expViewClaimChildren(vobj),\n"
                "... -- and the first that answers stops the chain.  The list sits\n"
                "on the object rather than on the view provider because a link\n"
                "property needs a document object to live in, and because one list\n"
                "then serves every view of the object.  Its links are Hidden scope:\n"
                "a reference, not a dependency, so an extension may read this object\n"
                "back without closing a cycle -- and, unlike ProxyExp, a copy with\n"
                "dependencies does not take the extension along.");
        // a reference, not a dependency: out of the out-list and the
        // back-links, but still saved, restored and broken on delete
        ViewProxyExp.setScope(LinkScope::Hidden);
        // Prop_NoRecompute only spares the object the Enforce bit; the touch
        // itself is what a view-side edit must not do, and Property::Output is
        // how DocumentObject::onChanged is told so -- the same answer
        // DocumentObject gives for Visibility.
        ViewProxyExp.setStatus(Property::Output, true);
        // cannot move this to the initializer list to avoid warning
        imp = new FeaturePythonImp(this);
    }
    ~FeaturePythonT() override {
        delete imp;
    }

    /** @name methods override DocumentObject */
    //@{
    short mustExecute() const override {
        if (this->isTouched())
            return 1;
        auto ret = FeatureT::mustExecute();
        if(ret) return ret;
        return imp->mustExecute()?1:0;
    }
    /// recalculate the Feature
    DocumentObjectExecReturn *execute() override {
        try {
            bool handled = imp->execute();
            if (!handled)
                return FeatureT::execute();
        }
        catch (const Base::Exception& e) {
            return new App::DocumentObjectExecReturn(e.what());
        }
        return DocumentObject::StdReturn;
    }
    bool skipRecompute() override {
        return imp->skipRecompute() && FeatureT::skipRecompute();
    }
    /// recalculate the Feature
    const char* getViewProviderNameOverride() const override {
        viewProviderName = imp->getViewProviderName();
        if(!viewProviderName.empty())
            return viewProviderName.c_str();
        return FeatureT::getViewProviderNameOverride();
    }
    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override {
        return FeatureT::getViewProviderName();
        //return "Gui::ViewProviderFeaturePython";
    }

    App::DocumentObject *getSubObject(const char *subname, PyObject **pyObj,
            Base::Matrix4D *mat, bool transform, int depth) const override 
    {
        App::DocumentObject *ret = nullptr;
        if(imp->getSubObject(ret,subname,pyObj,mat,transform,depth))
            return ret;
        return FeatureT::getSubObject(subname,pyObj,mat,transform,depth);
    }

    std::vector<std::string> getSubObjects(int reason=0) const override {
        std::vector<std::string> ret;
        if(imp->getSubObjects(ret,reason))
            return ret;
        return FeatureT::getSubObjects(reason);
    }

    App::DocumentObject *getLinkedObject(bool recurse,
            Base::Matrix4D *mat, bool transform, int depth) const override
    {
        App::DocumentObject *ret = nullptr;
        if(imp->getLinkedObject(ret,recurse,mat,transform,depth))
            return ret;
        return FeatureT::getLinkedObject(recurse,mat,transform,depth);
    }

    /// return true to activate tree view group object handling
    bool hasChildElement() const override {
        switch (imp->hasChildElement()) {
        case FeaturePythonImp::Accepted:
            return true;
        case FeaturePythonImp::Rejected:
            return false;
        default:
            return FeatureT::hasChildElement();
        }
    }
    /// Get sub-element visibility
    int isElementVisible(const char *element) const override {
        int ret = imp->isElementVisible(element);
        if(ret == -2)
            return FeatureT::isElementVisible(element);
        return ret;
    }
    /// Get sub-object/element visibility
    int isElementVisibleEx(const char *subname, int reason) const override {
        int ret = imp->isElementVisibleEx(subname, reason);
        if(ret == -2)
            return FeatureT::isElementVisibleEx(subname,reason);
        return ret;
    }
    /// Set sub-element visibility
    int setElementVisible(const char *element, bool visible) override {
        int ret = imp->setElementVisible(element,visible);
        if(ret == -2)
            return FeatureT::setElementVisible(element,visible);
        return ret;
    }

    bool canLinkProperties() const override {
        switch (imp->canLinkProperties()) {
        case FeaturePythonImp::Accepted:
            return true;
        case FeaturePythonImp::Rejected:
            return false;
        default:
            return FeatureT::canLinkProperties();
        }
    }

    bool allowDuplicateLabel() const override {
        switch (imp->allowDuplicateLabel()) {
        case FeaturePythonImp::Accepted:
            return true;
        case FeaturePythonImp::Rejected:
            return false;
        default:
            return FeatureT::allowDuplicateLabel();
        }
    }

    bool redirectSubName(std::ostringstream &ss,
            App::DocumentObject *topParent, App::DocumentObject *child) const override 
    {
        switch (imp->redirectSubName(ss,topParent,child)) {
        case FeaturePythonImp::Accepted:
            return true;
        case FeaturePythonImp::Rejected:
            return false;
        default:
            return FeatureT::redirectSubName(ss, topParent, child);
        }
    }

    int canLoadPartial() const override {
        int ret = imp->canLoadPartial();
        if(ret>=0)
            return ret;
        return FeatureT::canLoadPartial();
    }

    std::string getElementMapVersion(const App::Property *prop, bool restored=false) const override {
        std::string ver = FeatureT::getElementMapVersion(prop, restored);
        imp->getElementMapVersion(ver, prop, restored);
        return ver;
    }

    void editProperty(const char *propName) override {
        if (!imp->editProperty(propName))
            FeatureT::editProperty(propName);
    }

    PyObject *getPyObject() override {
        if (FeatureT::PythonObject.is(Py::_None())) {
            // ref counter is set to 1
            FeatureT::PythonObject = Py::Object(imp->getPyObject(),true);
        }
        return Py::new_reference_to(FeatureT::PythonObject);
    }
    void setPyObject(PyObject *obj) override {
        if (obj)
            FeatureT::PythonObject = obj;
        else
            FeatureT::PythonObject = Py::None();
    }

protected:
    void onBeforeChange(const Property* prop) override {
        FeatureT::onBeforeChange(prop);
        imp->onBeforeChange(prop);
    }
    void onBeforeChangeLabel(std::string &newLabel) override{
        if(!imp->onBeforeChangeLabel(newLabel))
            FeatureT::onBeforeChangeLabel(newLabel);
    }
    void onChanged(const Property* prop) override {
        if(prop == &Proxy)
            imp->init(Proxy.getValue().ptr());
        else if(prop == &ProxyExp)
            imp->setHookExtensions(ProxyExp.getValues());
        imp->onChanged(prop);
        FeatureT::onChanged(prop);
    }
    void onDocumentRestored() override {
        // the links resolve late, so the list only means anything now
        imp->setHookExtensions(ProxyExp.getValues());
        imp->onDocumentRestored();
        FeatureT::onDocumentRestored();
    }
    void setupObject() override {
        FeatureT::setupObject();
        imp->setupObject();
    }
    void unsetupObject() override {
        imp->unsetupObject();
        FeatureT::unsetupObject();
    }

public:
    FeaturePythonT(const FeaturePythonT&) = delete;
    FeaturePythonT(FeaturePythonT&&) = delete;
    FeaturePythonT& operator= (const FeaturePythonT&) = delete;
    FeaturePythonT& operator= (FeaturePythonT&&) = delete;

private:
    FeaturePythonImp* imp;
    PropertyPythonObject Proxy;
    /// the chain of docs/ProxyChain.md: objects whose methods extend this one
    PropertyXLinkList ProxyExp;
    /// the same, for the hooks of this object's VIEW PROVIDER; read by
    /// Gui::ViewProviderFeaturePythonT, which holds no list of its own
    PropertyXLinkList ViewProxyExp;
    mutable std::string viewProviderName;
};

// Special Feature-Python classes
using FeaturePython  = FeaturePythonT<DocumentObject>;
using GeometryPython = FeaturePythonT<GeoFeature    >;

} //namespace App

#endif // APP_FEATUREPYTHON_H
