/***************************************************************************
 *   Copyright (c) 2015 Eivind Kvedalen <eivind@kvedalen.name>             *
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

#ifndef EXPRESSION_H
#define EXPRESSION_H

#include <deque>
#include <list>
#include <set>
#include <string>
#include <boost/pool/pool_alloc.hpp>
#include <boost/tuple/tuple.hpp>
#include <App/PropertyLinks.h>
#include <App/ObjectIdentifier.h>
#include <App/Range.h>
#include <Base/Exception.h>
#include <Base/BaseClass.h>
#include <Base/Unit.h>

namespace Base {
class XMLReader;
class Quantity;
}

namespace App  {

class DocumentObject;
class Expression;
class Document;

using ExpressionPtr = std::unique_ptr<Expression>;

AppExport bool isAnyEqual(const App::any &v1, const App::any &v2);
AppExport Base::Quantity anyToQuantity(const App::any &value, const char *errmsg = nullptr);

// Map of depending objects to a map of depending property name to the full referencing object identifier
using ExpressionDeps = std::map<App::DocumentObject*, std::map<std::string, std::vector<ObjectIdentifier> > >;

class AppExport ExpressionVisitor {
public:
    virtual ~ExpressionVisitor() = default;
    virtual void visit(Expression &e) = 0;
    virtual void aboutToChange() {}
    virtual int changed() const { return 0;}
    virtual void reset() {}
    virtual App::PropertyLinkBase* getPropertyLink() {return nullptr;}

protected:
    void getIdentifiers(Expression &e, std::map<App::ObjectIdentifier, bool> &); 
    bool adjustLinks(Expression &e, const std::set<App::DocumentObject*> &inList);
    bool relabeledDocument(Expression &e, const std::string &oldName, const std::string &newName);
    bool renameObjectIdentifier(Expression &e,
            const std::map<ObjectIdentifier,ObjectIdentifier> &, const ObjectIdentifier &);
    void collectReplacement(Expression &e, std::map<ObjectIdentifier,ObjectIdentifier> &,
            const App::DocumentObject *parent, App::DocumentObject *oldObj, App::DocumentObject *newObj) const;
    bool updateElementReference(Expression &e, App::DocumentObject *feature,bool reverse);
    void importSubNames(Expression &e, const ObjectIdentifier::SubNameMap &subNameMap);
    void updateLabelReference(Expression &e, App::DocumentObject *obj,
            const std::string &ref, const char *newLabel);
    void moveCells(Expression &e, const CellAddress &address, int rowCount, int colCount);
    void offsetCells(Expression &e, int rowOffset, int colOffset);
    void transposeCells(Expression &e,
                        const CellAddress &origin,
                        const CellAddress &src,
                        const CellAddress &dst);
};

template<class P> class ExpressionModifier : public ExpressionVisitor {
public:
    explicit ExpressionModifier(P & _prop)
        : prop(_prop)
        , propLink(Base::freecad_dynamic_cast<App::PropertyLinkBase>(&prop))
        , signaller(_prop,false)
        , _changed(0)
    {}

    ~ExpressionModifier() override = default;

    void aboutToChange() override{
        ++_changed;
        signaller.aboutToChange();
    }

    int changed() const override { return _changed; }

    void reset() override {_changed = 0;}

    App::PropertyLinkBase* getPropertyLink() override {return propLink;}

protected:
    P & prop;
    App::PropertyLinkBase *propLink;
    typename AtomicPropertyChangeInterface<P>::AtomicPropertyChange signaller;
    int _changed;
};

// The reason of not using boost::pool_allocator directly is because the one
// come from boost 1.55 lacks proper support of std::move(), which is required
// in order to support vector of unique_ptr. Specifically, it lacks a
// forwarding construct() which is provided by this class.
//
// WARNING!!! Another change here is to default not using mutex. Assuming FC is
// (mostly) single threaded
#define _ExpressionAllocDefine(_name,_parent) \
template<typename T,\
    typename UserAllocator = boost::default_user_allocator_new_delete,\
    typename Mutex = boost::details::pool::null_mutex,\
    unsigned NextSize = 32,\
    unsigned MaxSize = 8192 > \
class _name: \
    public _parent<T, UserAllocator, Mutex, NextSize, MaxSize>\
{\
    typedef _parent<T, UserAllocator, Mutex, NextSize, MaxSize> inherited;\
public:\
    template <typename U>\
    struct rebind { \
      typedef _name<U, UserAllocator, Mutex, NextSize, MaxSize> other;\
    };\
    _name():inherited() {}\
    template <typename U>\
    _name(const _name<U, UserAllocator, Mutex, NextSize, MaxSize> &other)\
        :inherited(other)\
    {}\
    template <typename U, typename... Args>\
    static void construct(U* ptr, Args&&... args)\
    { new (ptr) U(std::forward<Args>(args)...); }\
}

_ExpressionAllocDefine(_ExpressionAllocator,boost::pool_allocator);
#define ExpressionAllocator(_t) _ExpressionAllocator<_t>

/**
  * Base class for expressions.
  *
  */

class AppExport Expression : public Base::BaseClass {
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:

    Expression(const Expression&) = delete;
    void operator=(const Expression &)=delete;

    explicit Expression(const App::DocumentObject * _owner);

    ~Expression() override;

    virtual bool isTouched() const { return false; }

    enum EvalOption {
        OptionCallFrame = 1,
        OptionPythonMode = 2,
    };
    ExpressionPtr eval(int options=0) const;

    App::any getValueAsAny(int options=0) const;

    Py::Object getPyValue(int options=0, int *jumpCode=0) const;

    bool isSame(const Expression &other, bool checkComment=true) const;

    std::string toString(bool persistent=false, bool checkPriority=false, int indent=0) const;

    void toString(std::ostream &ss, bool persistent=false, bool checkPriority=false, int indent=0) const;

    struct StringMaker {
        const Expression &e;
        bool persistent;
        bool checkPriority;
        int indent;

        inline StringMaker(const Expression &e, bool persistent, bool checkPriority, int indent)
            :e(e),persistent(persistent),checkPriority(checkPriority),indent(indent)
        {}

        friend inline std::ostream &operator<<(std::ostream &ss, const StringMaker &maker) {
            maker.e.toString(ss,maker.persistent,maker.checkPriority,maker.indent);
            return ss;
        }
    };
    inline StringMaker toStr(bool persistent=false, bool checkPriority=false, int indent=0) const {
        return StringMaker(*this,persistent,checkPriority,indent);
    }

    ExpressionPtr copy() const;

    static ExpressionPtr parse(const App::DocumentObject * owner, 
            const char* buffer, size_t len=0, bool verbose=false, bool pythonMode=false);

    static ExpressionPtr parse(const App::DocumentObject * owner, 
            const std::string &buffer, bool verbose=false, bool pythonMode=false) 
    {
        return parse(owner,buffer.c_str(),buffer.size(),verbose,pythonMode);
    }

    static ExpressionPtr parseUnit(const App::DocumentObject * owner, const char* buffer);

    static ExpressionPtr parseUnit(const App::DocumentObject * owner, const std::string &buffer) {
        return parseUnit(owner,buffer.c_str());
    }

    virtual int priority() const;

    void getIdentifiers(std::map<App::ObjectIdentifier,bool> &) const;
    std::map<App::ObjectIdentifier,bool> getIdentifiers() const;

    enum DepOption {
        DepNormal,
        DepHidden,
        DepAll,
    };
    void getDeps(ExpressionDeps &deps, int option=DepNormal) const;
    ExpressionDeps getDeps(int option=DepNormal) const;

    std::map<App::DocumentObject*,bool> getDepObjects(std::vector<std::string> *labels=nullptr) const;
    void getDepObjects(std::map<App::DocumentObject*,bool> &, std::vector<std::string> *labels=nullptr) const;

    ExpressionPtr importSubNames(const std::map<std::string,std::string> &nameMap) const;

    ExpressionPtr updateLabelReference(App::DocumentObject *obj,
            const std::string &ref, const char *newLabel) const;

    ExpressionPtr replaceObject(const App::DocumentObject *parent,
            App::DocumentObject *oldObj, App::DocumentObject *newObj) const;

    bool adjustLinks(const std::set<App::DocumentObject*> &inList);

    virtual ExpressionPtr simplify() const { return copy(); }

    void visit(ExpressionVisitor & v);

    class Exception : public Base::Exception {
    public:
        explicit Exception(const char *sMessage) : Base::Exception(sMessage) { }
    };

    App::DocumentObject *getOwner() const { return owner; }

    struct Component;
    typedef std::unique_ptr<Component> ComponentPtr;

    virtual void addComponent(ComponentPtr &&component);

    using ComponentList = std::vector<ComponentPtr, ExpressionAllocator(ComponentPtr) >;
    using StringList = std::vector<std::string, ExpressionAllocator(std::string) >;
    using ExpressionList = std::vector<ExpressionPtr, ExpressionAllocator(ExpressionPtr) >;

    static ComponentPtr createComponent(const std::string &n);
    static ComponentPtr createComponent(ExpressionPtr &&e1, ExpressionPtr &&e2=ExpressionPtr(), 
            ExpressionPtr &&e3=ExpressionPtr(), bool isRange=false);

    virtual bool needLineEnd() const {return false;};

    bool hasComponent() const {return !components.empty();}

protected:
    virtual bool _isIndexable() const {return false;}
    virtual ExpressionPtr _copy() const = 0;
    virtual void _toString(std::ostream &ss, bool persistent, int indent) const = 0;
    virtual void _getIdentifiers(std::map<App::ObjectIdentifier,bool> &) const  {}
    virtual bool _adjustLinks(const std::set<App::DocumentObject*> &, ExpressionVisitor &) {return false;}
    virtual bool _updateElementReference(App::DocumentObject *,bool,ExpressionVisitor &) {return false;}
    virtual bool _relabeledDocument(const std::string &, const std::string &, ExpressionVisitor &) {return false;}
    virtual void _importSubNames(const ObjectIdentifier::SubNameMap &) {}
    virtual void _updateLabelReference(App::DocumentObject *, const std::string &, const char *) {}
    virtual bool _renameObjectIdentifier(const std::map<ObjectIdentifier,ObjectIdentifier> &,
                                         const ObjectIdentifier &, ExpressionVisitor &) {return false;}
    virtual void _collectReplacement(std::map<ObjectIdentifier,ObjectIdentifier> &,
        const App::DocumentObject *parent, App::DocumentObject *oldObj, App::DocumentObject *newObj) const
    {
        (void)parent;
        (void)oldObj;
        (void)newObj;
    }
    virtual void _moveCells(const CellAddress &, int, int, ExpressionVisitor &) {}
    virtual void _offsetCells(int, int, ExpressionVisitor &) {}
    virtual void _transposeCells(const CellAddress & /*origin*/,
                                 const CellAddress & /*src*/,
                                 const CellAddress & /*dst*/,
                                 ExpressionVisitor &)
    {}
    virtual Py::Object _getPyValue(int *jumpCode=nullptr) const = 0;
    virtual void _visit(ExpressionVisitor &) {}

    void swapComponents(Expression &other) {components.swap(other.components);}

    friend ExpressionVisitor;

protected:
    App::DocumentObject * owner; /**< The document object used to access unqualified variables (i.e local scope) */

    ComponentList components;

public:
    std::string comment;
};

} // end of namespace App

#endif // EXPRESSION_H
