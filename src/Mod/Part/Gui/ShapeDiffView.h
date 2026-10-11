/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef PARTGUI_SHAPEDIFFVIEW_H
#define PARTGUI_SHAPEDIFFVIEW_H

#include <map>
#include <memory>
#include <string>

#include <Gui/InventorBase.h>
#include <Mod/Part/PartGlobal.h>

class SoSwitch;
class SoTransform;

namespace Gui
{
class SoFCPathAnnotation;
}

namespace Part
{
class TopoShape;
}

namespace PartGui
{

class ViewProviderPartExt;

/** Two states of an object's shape, drawn over the object
 * (docs/TransactionLog.md sec 32.9).
 *
 * What Part::ShapeDiff says of the two is drawn as PartDesign's preview of a
 * feature is: the faces of each kind are a shape of their own, shown by a
 * Part view provider of the object that is no view provider of the document,
 * as an annotation over the view. Nothing of the document is touched -- no
 * object, no property, no undo step.
 *
 * A is the earlier state and B the later: what both have unchanged is grey
 * and thin; what changed is drawn as it is in each; what only B has and what
 * only A has, each in a colour of its own. The position goes from A, 0, to
 * B, 1: the side it is at is drawn full and the other ghosted.
 */
class PartGuiExport ShapeDiffView
{
public:
    explicit ShapeDiffView(ViewProviderPartExt& owner);
    ~ShapeDiffView();

    ShapeDiffView(const ShapeDiffView&) = delete;
    ShapeDiffView& operator=(const ShapeDiffView&) = delete;

    /// Draw \a a against \a b; false where neither has a face
    bool show(const Part::TopoShape& a, const Part::TopoShape& b);
    /// Where between A and B, and whether what did not change is left out
    void setOptions(float position, bool differencesOnly);
    /// The object moved: the two states stay where they are
    void updatePlacement();

    /** What is shown: how many faces are `same`, `moved`, `changed`,
     * `added` and `removed`, how many were paired by each of
     * Part::ShapeDiff's stages (`by.name` ...), `position` in percent,
     * `differencesOnly`, and for each kind drawn whether it is to be seen
     * (`shown.same` ...) and how transparent (`transparency.same` ...).
     */
    const std::map<std::string, int>& summary() const
    {
        return _summary;
    }

private:
    /// The kinds of faces drawn, each a shape of its own
    enum Kind
    {
        Same,     ///< B's, as they were in A
        ChangedA, ///< A's that are other in B, or elsewhere
        ChangedB, ///< the same, as B has them
        Removed,  ///< A's alone
        Added,    ///< B's alone
        Kinds
    };
    static const char* name(Kind kind);
    void apply();

    ViewProviderPartExt& _owner;
    Gui::CoinPtr<Gui::SoFCPathAnnotation> _root;
    Gui::CoinPtr<SoTransform> _transform;
    Gui::CoinPtr<SoSwitch> _switch[Kinds];
    std::unique_ptr<ViewProviderPartExt> _view[Kinds];
    float _position = 1.f;
    bool _differencesOnly = false;
    std::map<std::string, int> _summary;
};

}  // namespace PartGui

#endif  // PARTGUI_SHAPEDIFFVIEW_H
