/******************************************************************************
 *   Copyright (c) 2012 Konstantinos Poulios <logari81@gmail.com>             *
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


#ifndef GUI_ReferenceSelection_H
#define GUI_ReferenceSelection_H

#include <Gui/SelectionFilter.h>
#include <Mod/PartDesign/Gui/EnumFlags.h>

class QLabel;

namespace App {
class OriginGroupExtension;
}
namespace PartDesign {
class Body;
}
namespace PartDesignGui {

class ReferenceSelection : public Gui::SelectionFilterGate
{
public:

    static AllowSelectionFlags defaultFlags() {
        return AllowSelection::FACE
             | AllowSelection::OTHERBODY;
    }

private:
    const App::DocumentObject* support;
    AllowSelectionFlags type;

public:
    ReferenceSelection(const App::DocumentObject* support_,
                       AllowSelectionFlags type = defaultFlags())
        : Gui::SelectionFilterGate(nullPointer())
        , support(support_)
        , type(type)
    {
    }
    /**
      * Allow the user to pick only edges or faces (or both) from the defined support
      * Optionally restrict the selection to planar edges/faces
      */
    bool allow(App::Document* pDoc, App::DocumentObject* pObj, const char* sSubName) override;

private:
    PartDesign::Body* getBody() const;
    App::OriginGroupExtension* getOriginGroupExtension(PartDesign::Body *body) const;
    bool allowOrigin(PartDesign::Body *body, App::OriginGroupExtension* originGroup, App::DocumentObject* pObj) const;
    bool allowDatum(PartDesign::Body *body, App::DocumentObject* pObj) const;
    bool allowPartFeature(App::DocumentObject* pObj, const char* sSubName) const;
    bool allowFeature(App::DocumentObject* pObj, const char* sSubName) const;
    bool isEdge(App::DocumentObject* pObj, const char* sSubName) const;
    bool isFace(App::DocumentObject* pObj, const char* sSubName) const;
    bool isCircle(App::DocumentObject* pObj, const char* sSubName) const;
    bool isWire(App::DocumentObject* pObj, const char* sSubName) const;
};

class NoDependentsSelection : public Gui::SelectionFilterGate
{
    std::set<App::DocumentObject *> inList;

public:
    NoDependentsSelection(const App::DocumentObject* support_)
        : Gui::SelectionFilterGate(nullPointer())
    {
        if(support_) {
            inList = support_->getInListEx(true);
            inList.insert(const_cast<App::DocumentObject*>(support_));
        }
    }
    /**
    * Allow the user to pick only objects which are not in objs getDependencyList
    */
    bool allow(App::Document* pDoc, App::DocumentObject* pObj, const char* sSubName) override;
};

class CombineSelectionFilterGates: public Gui::SelectionFilterGate
{
    std::unique_ptr<Gui::SelectionFilterGate> filter1;
    std::unique_ptr<Gui::SelectionFilterGate> filter2;

public:
    CombineSelectionFilterGates(std::unique_ptr<Gui::SelectionFilterGate> &filter1_, std::unique_ptr<Gui::SelectionFilterGate> &filter2_)
        : Gui::SelectionFilterGate(nullPointer()), filter1(std::move(filter1_)), filter2(std::move(filter2_))
    {
    }
    bool allow(App::Document* pDoc, App::DocumentObject* pObj, const char* sSubName) override;
};
// Convenience methods
/// Extract reference from Selection
bool getReferencedSelection(const App::DocumentObject* thisObj, const Gui::SelectionChanges& msg,
                            App::DocumentObject*& selObj, std::vector<std::string>& selSub);
/// Return reference as string for UI elements (format <obj>:<subelement>
QString getRefStr(const App::DocumentObject* obj, const std::vector<std::string>& sub);
/// Return reference as string for python in the format (<obj> ["sub1", "sub2", ...])
std::string buildLinkSubPythonStr(const App::DocumentObject* obj, const std::vector<std::string>& subs);
/// Return reference as string for python in the format (<obj> ["sub"?])
std::string buildLinkSingleSubPythonStr(const App::DocumentObject* obj, const std::vector<std::string>& subs);
/// Return reference as string for python in the format [obj1, obj2, ...,]
std::string buildLinkListPythonStr(const std::vector<App::DocumentObject*> & objs);
/// Returns sub reference list as a python string in the format [(obj1,"sub1"),(obj2,"sub2"),...]
std::string buildLinkSubListPythonStr(const std::vector<App::DocumentObject*> & objs,
        const std::vector<std::string>& subs);

/** Populate the label widget with the give property link sub
 * @param prop: input property
 * @param label: label widget
 * @param canTouch: if set to True, the check if the element reference exists,
 *                  and try to guess a related element if not found.
 *
 * @return Return true if the property is changed due to missing element.
 */
bool populateRefElement(App::PropertyLinkSub *prop, QLabel *label, bool canTouch);

} //namespace PartDesignGui

#endif // GUI_ReferenceSelection_H
