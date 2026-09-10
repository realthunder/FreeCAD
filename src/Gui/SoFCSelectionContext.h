/****************************************************************************
 *   Copyright (c) 2018 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef GUI_SOFCSELECTIONCONTEXT_H
#define GUI_SOFCSELECTIONCONTEXT_H

#include <climits>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <Inventor/SbColor.h>

#include <App/MaterialAppearance.h>

class SoState;

namespace Gui {

class SoFCSelectionRoot;
struct SoFCSelectionContextBase;
using SoFCSelectionContextBasePtr = std::shared_ptr<SoFCSelectionContextBase>;

struct GuiExport SoFCSelectionContextBase {
    std::shared_ptr<int> counter;

    virtual ~SoFCSelectionContextBase() = default;
    using MergeFunc = int (int status,
                           SoFCSelectionContextBasePtr &output,
                           SoFCSelectionContextBasePtr input,
                           SoNode *node);

    virtual bool isCounted() const = 0;
};

struct SoFCSelectionContext;
using SoFCSelectionContextPtr = std::shared_ptr<SoFCSelectionContext>;

struct GuiExport SoFCSelectionContext : SoFCSelectionContextBase
{
    std::set<int> highlightIndex;
    std::map<int,int> selectionIndex; // map from index to reference count
    SbColor selectionColor;
    SbColor highlightColor;

    ~SoFCSelectionContext() override;

    bool isCounted() const override {
        return isSelected();
    }

    bool isSelected() const {
        return !selectionIndex.empty();
    }

    bool hasSelectionColor() const {
        return selectionColor[0]!=0.0f
            || selectionColor[1]!=0.0f
            || selectionColor[2]!=0.0f;
    }

    void selectAll() {
        selectionIndex.clear();
        selectionIndex.emplace(-1,0);
    }

    bool isSelectAll() const{
        return !selectionIndex.empty() && selectionIndex.begin()->first < 0;
    }

    bool isHighlighted() const {
        return !highlightIndex.empty();
    }

    bool isHighlightAll() const{
        return highlightIndex.size()
            && *highlightIndex.begin() < 0
            && (!isSelected() || isSelectAll());
    }

    void highlightAll() {
        highlightIndex.clear();
        highlightIndex.insert(-1);
    }

    void removeHighlight() {
        highlightIndex.clear();
    }

    bool removeIndex(int index);
    bool addIndex(int index);

    bool checkGlobal(SoFCSelectionContextPtr ctx);

    virtual SoFCSelectionContextBasePtr copy() {
        return std::make_shared<SoFCSelectionContext>(*this);
    }

    static MergeFunc merge;
};

struct SoFCSelectionContextEx;
using SoFCSelectionContextExPtr = std::shared_ptr<SoFCSelectionContextEx>;

struct GuiExport SoFCSelectionContextEx : SoFCSelectionContext
{
    std::map<int,App::Color> colors;
    float trans0 = 0.0;

    bool setColors(const std::map<std::string,App::Color> &colors, const std::string &element);
    uint32_t packColor(const App::Color &c, bool &hasTransparency);
    bool applyColor(int idx, std::vector<uint32_t> &packedColors, bool &hasTransparency);
    bool isSingleColor(uint32_t &color, bool &hasTransparency);

    SoFCSelectionContextBasePtr copy() override {
        return std::make_shared<SoFCSelectionContextEx>(*this);
    }

    static MergeFunc merge;

    bool isCounted() const override {
        return !colors.empty() || isSelected();
    }
};

class SoHighlightElementAction;
class SoSelectionElementAction;

/** Helper class to reference count selection in various selection context
 *
 * The caching problem appears when the same node appears in different part of
 * the scene graph, and only some are selected. The caching node may also be
 * shared in different part of the scene.
 *
 * This class is an non-ideal word around of this problem. It keeps record of
 * the number of selections, and invalidates cache in case there are selections.
 */
class GuiExport SoFCSelectionCounter {
public:
    SoFCSelectionCounter();
    virtual ~SoFCSelectionCounter();

    /// Invalids cache if there are selections
    bool checkCache(SoState *state, bool secondary=false);

    /// Count highlight action
    void checkAction(SoHighlightElementAction *hlaction);

    /// Count selection action
    void checkAction(SoSelectionElementAction *selaction,
            SoFCSelectionContextBasePtr ctx = SoFCSelectionContextBasePtr());

protected:
    std::shared_ptr<int> counter;
    bool hasSelection{false};
    bool hasPreselection{false};
    static int cachingMode;
};

}
#endif //GUI_SOFCSELECTIONCONTEXT_H
