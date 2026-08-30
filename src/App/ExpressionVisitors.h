/***************************************************************************
 *   Copyright (c) 2016 Eivind Kvedalen <eivind@kvedalen.name>             *
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

#ifndef RENAMEOBJECTIDENTIFIEREXPRESSIONVISITOR_H
#define RENAMEOBJECTIDENTIFIEREXPRESSIONVISITOR_H

#include <functional>
#include "Expression.h"
#ifndef FC_EXPR_IMAGE
// The modifier visitors below tie expressions to the property/link and
// document systems; only GenericExpressionVisitor (at the bottom) is part
// of the core surface the sandbox image compiles.
#include "Document.h"
#include "PropertyLinks.h"
#endif

namespace App {

#ifndef FC_EXPR_IMAGE

/** Base class of visitors that modify an expression while it is owned by a
 * property, taking care of the property change signalling. Lives here, on
 * the host-only side of the expression code, because it ties expressions to
 * the property/link system; the expression core (Expression.h) must not
 * depend on App/PropertyLinks.h.
 */
template<class P> class ExpressionModifier : public ExpressionVisitor {
public:
    explicit ExpressionModifier(P & _prop)
        : prop(_prop)
        , propLink(Base::freecad_dynamic_cast<App::PropertyLinkBase>(&prop))
        , signaller(_prop,false)
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
    int _changed{0};
};

/**
 * @brief The RenameObjectIdentifierExpressionVisitor class is a functor used to visit each node of an expression, and
 * possibly rename VariableExpression nodes.
 */

template<class P> class RenameObjectIdentifierExpressionVisitor : public ExpressionModifier<P> {
public:
    RenameObjectIdentifierExpressionVisitor(
        P &_prop,
        const std::map<ObjectIdentifier, ObjectIdentifier> &_paths,
        const ObjectIdentifier &_owner)
        : ExpressionModifier<P>(_prop)
        , paths( _paths )
        , owner( _owner )
    {
    }

    void visit(Expression &node) override {
        this->renameObjectIdentifier(node,paths,owner);
    }

private:
   const std::map<ObjectIdentifier, ObjectIdentifier> &paths; /**< Map with current and new object identifiers */
   const ObjectIdentifier owner;                              /**< Owner of expression */
};

template<class P> class UpdateElementReferenceExpressionVisitor : public ExpressionModifier<P> {
public:

    explicit UpdateElementReferenceExpressionVisitor(P & _prop, App::DocumentObject *feature=nullptr, bool reverse=false)
        : ExpressionModifier<P>(_prop),feature(feature),reverse(reverse)
    {
    }

    void visit(Expression &node) override {
        this->updateElementReference(node,feature,reverse);
    }

private:
    App::DocumentObject *feature;
    bool reverse;
};

// Document relabel is not undoable, so we don't derive from
// ExpressionModifier, hence not calling aboutToSetValue/hasSetValue().
// By right, modification of document label should not change evaluation result
// of any expression.
class RelabelDocumentExpressionVisitor : public ExpressionVisitor {
public:

    explicit RelabelDocumentExpressionVisitor(const App::Document &doc)
         : doc(doc)
    {
    }

    void visit(Expression &node) override {
        this->relabeledDocument(node,doc.getOldLabel(),doc.Label.getStrValue());
    }

private:
    const App::Document &doc;
};

template<class P> class MoveCellsExpressionVisitor : public ExpressionModifier<P> {
public:
    MoveCellsExpressionVisitor(P &prop, const CellAddress &address, int rowCount, int colCount)
        : ExpressionModifier<P>(prop),address(address),rowCount(rowCount),colCount(colCount)
    {}

    void visit(Expression &node) override {
        this->moveCells(node,address,rowCount,colCount);
    }

private:
    CellAddress address;
    int rowCount;
    int colCount;
};

template<class P> class OffsetCellsExpressionVisitor : public ExpressionModifier<P> {
public:
    OffsetCellsExpressionVisitor(P &prop, int rowOffset, int colOffset)
        : ExpressionModifier<P>(prop),rowOffset(rowOffset),colOffset(colOffset)
    {}

    void visit(Expression &node) override {
        this->offsetCells(node,rowOffset,colOffset);
    }

private:
    int rowOffset;
    int colOffset;
};

template<class P> class TransposeCellsExpressionVisitor : public ExpressionModifier<P> {
public:
    TransposeCellsExpressionVisitor(P &prop,
                                    const CellAddress &origin,
                                    const CellAddress &src,
                                    const CellAddress &dst)
        : ExpressionModifier<P>(prop)
        , origin(origin)
        , src(src)
        , dst(dst)
    {}

    void visit(Expression &node) {
        this->transposeCells(node,origin,src,dst);
    }

private:
    CellAddress origin;
    CellAddress src;
    CellAddress dst;
};

#endif  // FC_EXPR_IMAGE

class GenericExpressionVisitor : public ExpressionVisitor {
public:
    typedef std::function<void(ExpressionVisitor*, Expression &)> FuncType;
    explicit GenericExpressionVisitor(FuncType f)
        :func(f)
    {}

    void visit(Expression &node) {
        func(this, node);
    }

private:
    FuncType func;
};

}

#endif // RENAMEOBJECTIDENTIFIEREXPRESSIONVISITOR_H
