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
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/PartGlobal.h>

class SoSwitch;
class SoTransform;

namespace Gui
{
class SoFCPathAnnotation;
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
 *
 * Or the two are drawn by their material (sec 32.16): the solid only A has,
 * the solid only B has, and thin and grey what both have. The faces say
 * which of them changed and this says where; it is what two solids have,
 * and of anything else the faces are drawn.
 */
class PartGuiExport ShapeDiffView
{
public:
    explicit ShapeDiffView(ViewProviderPartExt& owner);
    ~ShapeDiffView();

    ShapeDiffView(const ShapeDiffView&) = delete;
    ShapeDiffView& operator=(const ShapeDiffView&) = delete;

    /// What of the two states is drawn
    enum class Mode
    {
        Faces,    ///< what became of each face
        Material, ///< the material only one of them has; the faces where
                  ///< either is no solid
    };

    /** Draw \a a against \a b; false where neither has a face.
     *
     * \a refA and \a refB are what each is known by -- its ref in the log
     * -- and empty for one known by nothing but itself. Taking two solids
     * apart is a fuse of them, and what it gave is kept by what the two are
     * known by: going back to a row is not paid for again.
     *
     * Of two heavy solids the fuse is long, and is not waited for: their
     * faces are drawn, and the material in their place when it is there.
     */
    bool show(const Part::TopoShape& a, const Part::TopoShape& b, Mode mode = Mode::Faces,
              const std::string& refA = std::string(), const std::string& refB = std::string());
    /// The same two another way
    void setMode(Mode mode);
    /// Where between A and B, and whether what did not change is left out
    void setOptions(float position, bool differencesOnly);
    /// The object moved: the two states stay where they are
    void updatePlacement();
    /// What is drawn over \a owner's object changed, with no property of
    /// the document's changing: the clients of a served document are sent it
    static void sceneChanged(const ViewProviderPartExt& owner);

    /** What is shown. `material` is 1 where it is the material and 0 where
     * the faces; `material.none` is 1 where the material was asked for and
     * the two are not both solids, and `material.pending` 1 where it is
     * being worked out and the faces are shown meanwhile.
     *
     * Of the faces: how many are `same`, `moved`, `changed`, `added` and
     * `removed`, and how many were paired by each of Part::ShapeDiff's
     * stages (`by.name` ...). Of the material: `volume.removed`,
     * `volume.added` and `volume.common`, and `material.kept`, 1 where the
     * two were not fused for this but had been.
     *
     * Of either: `position` in percent, `differencesOnly`, and for each
     * kind drawn whether it is to be seen (`shown.same` ...) and how
     * transparent (`transparency.same` ...).
     */
    const std::map<std::string, double>& summary() const
    {
        return _summary;
    }

private:
    /// The kinds of faces drawn, each a shape of its own
    enum Kind
    {
        Same,     ///< B's, as they were in A; the material of both
        ChangedA, ///< A's that are other in B, or elsewhere
        ChangedB, ///< the same, as B has them
        Removed,  ///< A's alone
        Added,    ///< B's alone
        Kinds
    };
    static const char* name(Kind kind);
    bool build();
    void apply();

    ViewProviderPartExt& _owner;
    Gui::CoinPtr<Gui::SoFCPathAnnotation> _root;
    Gui::CoinPtr<SoTransform> _transform;
    Gui::CoinPtr<SoSwitch> _switch[Kinds];
    std::unique_ptr<ViewProviderPartExt> _view[Kinds];
    Part::TopoShape _a;
    Part::TopoShape _b;
    std::string _refA;
    std::string _refB;
    Mode _mode = Mode::Faces;
    bool _arrived = false;
    float _position = 1.f;
    bool _differencesOnly = false;
    std::map<std::string, double> _summary;
};

}  // namespace PartGui

#endif  // PARTGUI_SHAPEDIFFVIEW_H
