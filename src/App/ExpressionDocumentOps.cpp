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

// Document-maintenance operations of the expression engine: adjusting,
// renaming and relocating references when the surrounding document changes
// (link adjustment, label rename, object replace, spreadsheet cell moves),
// plus the persistence-restore context (ExpressionImporter's XMLReader push
// and ObjectIdentifier::String::checkImport). These never run during
// expression evaluation. They are split out of Expression.cpp and
// ObjectIdentifier.cpp so the evaluation core can be built without the
// property/link system and document persistence (expression sandbox,
// docs/ExpressionSandboxPhase0.md sec 2, seams S3/S10); this translation
// unit stays host-only.

#include "PreCompiled.h"

#include <cassert>
#include <sstream>
#include <string>

#include <Base/Console.h>
#include <Base/Reader.h>

#include "Document.h"
#include "DocumentObject.h"
#include "ExpressionParser.h"
#include "PropertyLinks.h"
#include "Range.h"

FC_LOG_LEVEL_INIT("Expression", true, true)

using namespace Base;
using namespace App;

////////////////////////////////////////////////////////////////////////////////////
//
// ExpressionVisitor maintenance entry points
//
bool ExpressionVisitor::adjustLinks(Expression &e, const std::set<App::DocumentObject*> &inList) {
    return e._adjustLinks(inList,*this);
}

void ExpressionVisitor::importSubNames(Expression &e, const ObjectIdentifier::SubNameMap &subNameMap) {
    e._importSubNames(subNameMap);
}

void ExpressionVisitor::updateLabelReference(Expression &e, 
        DocumentObject *obj, const std::string &ref, const char *newLabel) 
{
    e._updateLabelReference(obj,ref,newLabel);
}

bool ExpressionVisitor::updateElementReference(Expression &e, App::DocumentObject *feature, bool reverse) {
    return e._updateElementReference(feature,reverse,*this);
}

bool ExpressionVisitor::relabeledDocument(
        Expression &e, const std::string &oldName, const std::string &newName) 
{
    return e._relabeledDocument(oldName,newName,*this);
}

bool ExpressionVisitor::renameObjectIdentifier(Expression &e,
        const std::map<ObjectIdentifier,ObjectIdentifier> &paths, const ObjectIdentifier &path)
{
    return e._renameObjectIdentifier(paths,path,*this);
}

void ExpressionVisitor::collectReplacement(Expression &e,
        std::map<ObjectIdentifier,ObjectIdentifier> &paths,
        const App::DocumentObject *parent, App::DocumentObject *oldObj, App::DocumentObject *newObj) const
{
    return e._collectReplacement(paths,parent,oldObj,newObj);
}

void ExpressionVisitor::moveCells(Expression &e, const CellAddress &address, int rowCount, int colCount)
{
    e._moveCells(address,rowCount,colCount,*this);
}

void ExpressionVisitor::offsetCells(Expression &e, int rowOffset, int colOffset)
{
    e._offsetCells(rowOffset,colOffset,*this);
}

void ExpressionVisitor::transposeCells(Expression &e,
                                       const CellAddress &origin,
                                       const CellAddress &src,
                                       const CellAddress &dst)
{
    e._transposeCells(origin, src, dst, *this);
}


////////////////////////////////////////////////////////////////////////////////////
//
// Expression maintenance API
//
class AdjustLinksExpressionVisitor : public ExpressionVisitor {
public:
    explicit AdjustLinksExpressionVisitor(const std::set<App::DocumentObject*> &inList)
        :inList(inList)
    {}

    void visit(Expression &e) override {
        if(this->adjustLinks(e,inList))
            res = true;
    }

    const std::set<App::DocumentObject*> &inList;
    bool res{false};
};

bool Expression::adjustLinks(const std::set<App::DocumentObject*> &inList) {
    AdjustLinksExpressionVisitor v(inList);
    visit(v);
    return v.res;
}

class ImportSubNamesExpressionVisitor : public ExpressionVisitor {
public:
    explicit ImportSubNamesExpressionVisitor(const ObjectIdentifier::SubNameMap &subNameMap)
        :subNameMap(subNameMap)
    {}

    void visit(Expression &e) override {
        this->importSubNames(e,subNameMap);
    }

    const ObjectIdentifier::SubNameMap &subNameMap;
};

ExpressionPtr Expression::importSubNames(const std::map<std::string,std::string> &nameMap) const {
    if(!owner || !owner->getDocument())
        return nullptr;
    ObjectIdentifier::SubNameMap subNameMap;
    for(auto &dep : getDeps(DepAll)) {
        for(auto &info : dep.second) {
            for(auto &path : info.second) {
                auto obj = path.getDocumentObject();
                if(!obj)
                    continue;
                auto it = nameMap.find(obj->getExportName(true));
                if(it!=nameMap.end())
                    subNameMap.emplace(std::make_pair(obj,std::string()),it->second);
                auto key = std::make_pair(obj,path.getSubObjectName());
                if(key.second.empty() || subNameMap.count(key))
                    continue;
                std::string imported = PropertyLinkBase::tryImportSubName(
                               obj,key.second.c_str(),owner->getDocument(), nameMap);
                if(!imported.empty())
                    subNameMap.emplace(std::move(key),std::move(imported));
            }
        }
    }
    if(subNameMap.empty())
        return ExpressionPtr();
    ImportSubNamesExpressionVisitor v(subNameMap);
    auto res = copy();
    res->visit(v);
    return res;
}

class UpdateLabelExpressionVisitor : public ExpressionVisitor {
public:
    UpdateLabelExpressionVisitor(App::DocumentObject *obj, const std::string &ref, const char *newLabel)
        :obj(obj),ref(ref),newLabel(newLabel)
    {}

    void visit(Expression &e) override {
        this->updateLabelReference(e,obj,ref,newLabel);
    }

    App::DocumentObject *obj;
    const std::string &ref;
    const char *newLabel;
};

ExpressionPtr Expression::updateLabelReference(
        App::DocumentObject *obj, const std::string &ref, const char *newLabel) const 
{
    if(ref.size()<=2)
        return {};
    std::vector<std::string> labels;
    for(auto &v : getIdentifiers())
        v.first.getDepLabels(labels);
    for(auto &label : labels) {
        // ref contains something like $label. and we need to strip '$' and '.'
        if(ref.compare(1,ref.size()-2,label)==0) {
            UpdateLabelExpressionVisitor v(obj,ref,newLabel);
            ExpressionPtr expr(copy());
            expr->visit(v);
            return expr;
        }
    }
    return {};
}

class ReplaceObjectExpressionVisitor : public ExpressionVisitor {
public:
    ReplaceObjectExpressionVisitor(const DocumentObject *parent,
            DocumentObject *oldObj, DocumentObject *newObj)
        : parent(parent),oldObj(oldObj),newObj(newObj)
    {
    }

    void visit(Expression &e) override {
        if(collect)
            this->collectReplacement(e,paths,parent,oldObj,newObj);
        else
            this->renameObjectIdentifier(e,paths,dummy);
    }

    const DocumentObject *parent;
    DocumentObject *oldObj;
    DocumentObject *newObj;
    ObjectIdentifier dummy;
    std::map<ObjectIdentifier, ObjectIdentifier> paths;
    bool collect = true;
};

ExpressionPtr Expression::replaceObject(const DocumentObject *parent,
        DocumentObject *oldObj, DocumentObject *newObj) const
{
    ReplaceObjectExpressionVisitor v(parent,oldObj,newObj);

    // First pass, collect any changes. We have to const_cast it, as visit() is
    // not const. This is ugly...
    const_cast<Expression*>(this)->visit(v);

    if(v.paths.empty())
        return {};

    // Now make a copy and do the actual replacement
    ExpressionPtr expr(copy());
    v.collect = false;
    expr->visit(v);
    return expr;
}

////////////////////////////////////////////////////////////////////////////////////
//
// VariableExpression maintenance overrides
//
bool VariableExpression::_relabeledDocument(const std::string &oldName,
        const std::string &newName, ExpressionVisitor &v)
{
    return var.relabeledDocument(v, oldName, newName);
}

bool VariableExpression::_adjustLinks(
        const std::set<App::DocumentObject *> &inList, ExpressionVisitor &v) 
{
    return var.adjustLinks(v,inList);
}

void VariableExpression::_importSubNames(const ObjectIdentifier::SubNameMap &subNameMap) 
{
    var.importSubNames(subNameMap);
}

void VariableExpression::_updateLabelReference(
        App::DocumentObject *obj, const std::string &ref, const char *newLabel)
{
    var.updateLabelReference(obj,ref,newLabel);
}

bool VariableExpression::_updateElementReference(
        App::DocumentObject *feature, bool reverse, ExpressionVisitor &v)
{
    return var.updateElementReference(v,feature,reverse);
}

bool VariableExpression::_renameObjectIdentifier(
    const std::map<ObjectIdentifier, ObjectIdentifier>& paths,
    const ObjectIdentifier& path,
    ExpressionVisitor& visitor)
{
    const auto& oldPath = var.canonicalPath();
    auto it = paths.find(oldPath);
    if (it != paths.end()) {
        visitor.aboutToChange();
        const bool originalHasDocumentObjectName = var.hasDocumentObjectName();
        ObjectIdentifier::String originalDocumentObjectName = var.getDocumentObjectName();
        std::string originalSubObjectName = var.getSubObjectName();
        if (path.getOwner()) {
            var = it->second.relativeTo(path);
        }
        else {
            var = it->second;
        }
        if (originalHasDocumentObjectName) {
            var.setDocumentObjectName(std::move(originalDocumentObjectName),
                                      true,
                                      originalSubObjectName);
        }
        return true;
    }
    return false;
}

void VariableExpression::_collectReplacement(
        std::map<ObjectIdentifier,ObjectIdentifier> &paths,
        const App::DocumentObject *parent,
        App::DocumentObject *oldObj,
        App::DocumentObject *newObj) const
{
    ObjectIdentifier path;
    if(var.replaceObject(path,parent,oldObj,newObj))
        paths[var.canonicalPath()] = std::move(path);
}

void VariableExpression::_moveCells(const CellAddress &address,
        int rowCount, int colCount, ExpressionVisitor &v)
{
    if(var.hasDocumentObjectName(true))
        return;

    int idx = 0;
    const auto &comp = var.getPropertyComponent(0,&idx);
    CellAddress addr = stringToAddress(comp.getName().c_str(),true);
    if(!addr.isValid())
        return;

    int thisRow = addr.row();
    int thisCol = addr.col();
    if (thisRow >= address.row() || thisCol >= address.col()) {
        v.aboutToChange();
        addr.setRow(thisRow + rowCount);
        addr.setCol(thisCol + colCount);
        var.setComponent(idx,ObjectIdentifier::SimpleComponent(addr.toString()));
    }
}

void VariableExpression::_offsetCells(int rowOffset, int colOffset, ExpressionVisitor &v) {
    if(var.hasDocumentObjectName(true))
        return;

    int idx = 0;
    const auto &comp = var.getPropertyComponent(0,&idx);
    CellAddress addr = stringToAddress(comp.getName().c_str(),true);
    if(!addr.isValid() || (addr.isAbsoluteCol() && addr.isAbsoluteRow()))
        return;

    if(!addr.isAbsoluteCol())
        addr.setCol(addr.col()+colOffset);
    if(!addr.isAbsoluteRow())
        addr.setRow(addr.row()+rowOffset);
    if(!addr.isValid()) {
        FC_WARN("Not changing relative cell reference '"
                << comp.getName() << "' due to invalid offset "
                << '(' << colOffset << ", " << rowOffset << ')');
    } else {
        v.aboutToChange();
        var.setComponent(idx,ObjectIdentifier::SimpleComponent(addr.toString()));
    }
}

static void transposeCellAddress(CellAddress &addr,
                                 const CellAddress &origin,
                                 const CellAddress &src,
                                 const CellAddress &dst)
{
    if(!addr.isValid())
        return;

    int absRow = origin.row() + src.col() - origin.col();
    int absCol = origin.col() + src.row() - origin.row();
    int row = dst.row() + addr.col() - src.col();
    int col = dst.col() + addr.row() - src.row();

    if (addr.isAbsoluteCol() && addr.isAbsoluteRow())
        addr = CellAddress(absRow, absCol, /*absRow*/true, /*absCol*/true);
    else if (addr.isAbsoluteRow())
        addr = CellAddress(row, absCol, /*absRow*/false, /*absCol*/true);
    else if (addr.isAbsoluteCol())
        addr = CellAddress(absRow, col, /*absRow*/true, /*absCol*/false);
    else
        addr = CellAddress(row, col);
}

void VariableExpression::_transposeCells(const CellAddress &origin,
                                         const CellAddress &src,
                                         const CellAddress &dst,
                                         ExpressionVisitor &v)
{
    if(var.hasDocumentObjectName(true))
        return;

    int idx = 0;
    const auto &comp = var.getPropertyComponent(0,&idx);
    CellAddress addr = stringToAddress(comp.getName().c_str(),true);
    if(!addr.isValid())
        return;

    transposeCellAddress(addr, origin, src, dst);

    if(!addr.isValid()) {
        FC_WARN("Not changing relative cell reference '"
                << comp.getName() << "' due to invalid address after transpose");
    } else {
        v.aboutToChange();
        var.setComponent(idx,ObjectIdentifier::SimpleComponent(addr.toString()));
    }
}

////////////////////////////////////////////////////////////////////////////////////
//
// RangeExpression maintenance overrides
//
bool RangeExpression::_renameObjectIdentifier(
        const std::map<ObjectIdentifier,ObjectIdentifier> &paths, 
        const ObjectIdentifier &path, ExpressionVisitor &v)
{
    (void)path;
    bool touched =false;
    auto it = paths.find(ObjectIdentifier(owner,begin));
    if (it != paths.end()) {
        v.aboutToChange();
        begin = it->second.getPropertyName();
        touched = true;
    }
    it = paths.find(ObjectIdentifier(owner,end));
    if (it != paths.end()) {
        v.aboutToChange();
        end = it->second.getPropertyName();
        touched = true;
    }
    return touched;
}

void RangeExpression::_moveCells(const CellAddress &address,
        int rowCount, int colCount, ExpressionVisitor &v) 
{
    CellAddress addr = stringToAddress(begin.c_str(),true);
    if(addr.isValid()) {
        int thisRow = addr.row();
        int thisCol = addr.col();
        if (thisRow >= address.row() || thisCol >= address.col()) {
            v.aboutToChange();
            addr.setRow(thisRow+rowCount);
            addr.setCol(thisCol+colCount);
            begin = addr.toString();
        }
    }
    addr = stringToAddress(end.c_str(),true);
    if(addr.isValid()) {
        int thisRow = addr.row();
        int thisCol = addr.col();
        if (thisRow >= address.row() || thisCol >= address.col()) {
            v.aboutToChange();
            addr.setRow(thisRow + rowCount);
            addr.setCol(thisCol + colCount);
            end = addr.toString();
        }
    }
}

void RangeExpression::_offsetCells(int rowOffset, int colOffset, ExpressionVisitor &v) 
{
    CellAddress addr = stringToAddress(begin.c_str(),true);
    if(addr.isValid() && (!addr.isAbsoluteRow() || !addr.isAbsoluteCol())) {
        v.aboutToChange();
        if(!addr.isAbsoluteRow())
            addr.setRow(addr.row()+rowOffset, /*clip*/true);
        if(!addr.isAbsoluteCol()) 
            addr.setCol(addr.col()+colOffset, /*clip*/true);
        begin = addr.toString();
    }
    addr = stringToAddress(end.c_str(),true);
    if(addr.isValid() && (!addr.isAbsoluteRow() || !addr.isAbsoluteCol())) {
        v.aboutToChange();
        if(!addr.isAbsoluteRow())
            addr.setRow(addr.row()+rowOffset, /*clip*/true);
        if(!addr.isAbsoluteCol()) 
            addr.setCol(addr.col()+colOffset, /*clip*/true);
        end = addr.toString();
    }
}

void RangeExpression::_transposeCells(const CellAddress &origin,
                                      const CellAddress &src,
                                      const CellAddress &dst,
                                      ExpressionVisitor &v)
{
    CellAddress beginAddr = stringToAddress(end.c_str(),true);
    transposeCellAddress(beginAddr, origin, src, dst);

    CellAddress endAddr = stringToAddress(begin.c_str(),true);
    transposeCellAddress(endAddr, origin, src, dst);

    if (!beginAddr.isValid() || !endAddr.isValid()) {
        FC_WARN("Not changing relative range reference '"
                << toString() << "' due to invalid address after transpose");
        return;
    }

    Range range(beginAddr, endAddr, /*normalize*/true);
    std::string newBegin = range.from().toString();
    std::string newEnd = range.to().toString();
    if (newBegin != begin || newEnd != end) {
        v.aboutToChange();
        begin = std::move(newBegin);
        end = std::move(newEnd);
    }
}

////////////////////////////////////////////////////////////////////////////////////
//
// ObjectIdentifier document-maintenance and persistence-restore members
//
bool ObjectIdentifier::replaceObject(ObjectIdentifier &res, const App::DocumentObject *parent,
            App::DocumentObject *oldObj, App::DocumentObject *newObj) const
{
    ResolveResults result(*this);

    if(!result.resolvedDocumentObject)
        return false;

    auto r = PropertyLinkBase::tryReplaceLink(owner, result.resolvedDocumentObject,
            parent, oldObj, newObj, subObjectName.getString().c_str());

    if(!r.first)
        return false;

    res = *this;
    if(r.first != result.resolvedDocumentObject) {
        if(r.first->getDocument()!=owner->getDocument()) {
            auto doc = r.first->getDocument();
            bool useLabel = res.documentName.isRealString();
            const char *name = useLabel?doc->Label.getValue():doc->getName();
            res.setDocumentName(String(name, useLabel), true);
        }
        if(documentObjectName.isRealString())
            res.documentObjectName = String(r.first->Label.getValue(),true);
        else
            res.documentObjectName = String(r.first->getNameInDocument(),false,true);
    }
    res.subObjectName = String(r.second,true);
    res._cache.clear();
    res.shadowSub.first.clear();
    res.shadowSub.second.clear();
    return true;
}

bool ObjectIdentifier::updateLabelReference(
        App::DocumentObject *obj, const std::string &ref, const char *newLabel)
{
    if(!owner)
        return false;

    ResolveResults result(*this);

    if(!subObjectName.getString().empty() && result.resolvedDocumentObject) {
        std::string sub = PropertyLinkBase::updateLabelReference(
                result.resolvedDocumentObject, subObjectName.getString().c_str(), obj,ref,newLabel);
        if(!sub.empty()) {
            subObjectName = String(sub,true);
            _cache.clear();
            return true;
        }
    }

    if(result.resolvedDocument != obj->getDocument())
        return false;

    if(!documentObjectName.getString().empty()) {
        if(documentObjectName.isForceIdentifier())
            return false;

        if(!documentObjectName.isRealString() &&
           documentObjectName.getString()==obj->getNameInDocument())
            return false;

        if(documentObjectName.getString()!=obj->Label.getValue())
            return false;

        documentObjectName = ObjectIdentifier::String(newLabel, true);

        _cache.clear();
        return true;
    }

    if (result.resolvedDocumentObject==obj &&
        result.propertyIndex == 1 &&
        result.resolvedDocumentObjectName.isRealString() &&
        result.resolvedDocumentObjectName.getString()==obj->Label.getValue())
    {
        components[0].name = ObjectIdentifier::String(newLabel, true);
        _cache.clear();
        return true;
    }

    // If object identifier uses the label then resolving the document object will fail.
    // So, it must be checked if using the new label will succeed
    if (components.size()>1 && components[0].getName()==obj->Label.getValue()) {
        ObjectIdentifier id(*this);
        id.components[0].name.str = newLabel;

        ResolveResults result(id);

        if (result.propertyIndex == 1 && result.resolvedDocumentObject == obj) {
            components[0].name = id.components[0].name;
            _cache.clear();
            return true;
        }
    }

    return false;
}

bool ObjectIdentifier::relabeledDocument(ExpressionVisitor &v,
        const std::string &oldLabel, const std::string &newLabel)
{
    if (documentNameSet && documentName.isRealString() && documentName.getString()==oldLabel) {
        v.aboutToChange();
        documentName = String(newLabel,true);
        _cache.clear();
        return true;
    }
    return false;
}

void ObjectIdentifier::String::checkImport(const App::DocumentObject *owner,
        const App::DocumentObject *obj, String *objName)
{
    if(owner && owner->getDocument() && !str.empty() &&
       ExpressionParser::ExpressionImporter::reader()) {
        auto reader = ExpressionParser::ExpressionImporter::reader();
        if (obj || objName) {
            bool restoreLabel = false;
            str = PropertyLinkBase::importSubName(*reader,str.c_str(),restoreLabel);
            if (restoreLabel) {
                if (!obj) {
                    std::bitset<32> flags;
                    obj = getDocumentObject(owner->getDocument(),*objName,flags);
                    if (!obj) {
                        FC_ERR("Cannot find object " << objName->toString());
                    }
                }

                if (obj) {
                    PropertyLinkBase::restoreLabelReference(obj,str);
                }
            }
        }
        else if (str.back()!='@') {
            str = reader->getName(str.c_str());
        }
        else {
            str.resize(str.size()-1);
            auto mapped = reader->getName(str.c_str());
            auto objForMapped = owner->getDocument()->getObject(mapped);
            if (!objForMapped || objForMapped->testStatus(ObjectStatus::Remove)) {
                FC_ERR("Cannot find object " << str);
            }
            else {
                isString = true;
                forceIdentifier = false;
                str = objForMapped->Label.getValue();
            }
        }
    }
}

void ObjectIdentifier::importSubNames(const ObjectIdentifier::SubNameMap &subNameMap)
{
    if(!owner || !owner->getDocument())
        return;
    ResolveResults result(*this);
    auto it = subNameMap.find(std::make_pair(result.resolvedDocumentObject,std::string()));
    if(it!=subNameMap.end()) {
        auto obj = owner->getDocument()->getObject(it->second.c_str());
        if(!obj || obj->testStatus(ObjectStatus::Remove)) {
            FC_ERR("Failed to find import object " << it->second << " from "
                    << result.resolvedDocumentObject->getFullName());
            return;
        }
        documentNameSet = false;
        documentName.str.clear();
        if(documentObjectName.isRealString())
            documentObjectName.str = obj->Label.getValue();
        else
            documentObjectName.str = obj->getNameInDocument();
        _cache.clear();
    }
    if(subObjectName.getString().empty())
        return;
    it = subNameMap.find(std::make_pair(
                result.resolvedDocumentObject,subObjectName.str));
    if(it==subNameMap.end())
        return;
    subObjectName = String(it->second,true);
    _cache.clear();
    shadowSub.first.clear();
    shadowSub.second.clear();
}

bool ObjectIdentifier::updateElementReference(ExpressionVisitor &v,
        App::DocumentObject *feature, bool reverse)
{
    assert(v.getPropertyLink());
    if(subObjectName.getString().empty())
        return false;

    ResolveResults result(*this);
    if(!result.resolvedSubObject)
        return false;
    if(v.getPropertyLink()->_updateElementReference(
            feature,result.resolvedDocumentObject,subObjectName.str,shadowSub,reverse)) {
        _cache.clear();
        v.aboutToChange();
        return true;
    }
    return false;
}

bool ObjectIdentifier::adjustLinks(ExpressionVisitor &v, const std::set<App::DocumentObject *> &inList) {
    ResolveResults result(*this);
    if(!result.resolvedDocumentObject)
        return false;
    if(result.resolvedSubObject) {
        PropertyLinkSub prop;
        prop.setValue(result.resolvedDocumentObject, {subObjectName.getString()});
        if(prop.adjustLink(inList)) {
            v.aboutToChange();
            documentObjectName = String(prop.getValue()->getNameInDocument(),false,true);
            subObjectName = String(prop.getSubValues().front(),true);
            _cache.clear();
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////////
//
// Expression persistence-restore context (the XMLReader push during document load)
//
static Base::XMLReader *_Reader = nullptr;
ExpressionParser::ExpressionImporter::ExpressionImporter(Base::XMLReader &reader) {
    assert(!_Reader);
    _Reader = &reader;
}

ExpressionParser::ExpressionImporter::~ExpressionImporter() {
    assert(_Reader);
    _Reader = nullptr;
}

Base::XMLReader *ExpressionParser::ExpressionImporter::reader() {
    return _Reader;
}
