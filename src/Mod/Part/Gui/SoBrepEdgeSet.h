/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef PARTGUI_SOBREPEDGESET_H
#define PARTGUI_SOBREPEDGESET_H

#include <Inventor/fields/SoMFInt32.h>
#include <Inventor/fields/SoSFBool.h>
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <memory>
#include <vector>
#include <Gui/SoFCSelectionContext.h>
#include <Mod/Part/PartGlobal.h>
#include "BoundBoxRayPick.h"

class SoCoordinateElement;
class SoGLCoordinateElement;
class SoTextureCoordinateBundle;

namespace PartGui {

class PartGuiExport SoBrepEdgeSet : public SoIndexedLineSet {
    using inherited = SoIndexedLineSet;

    SO_NODE_HEADER(SoBrepEdgeSet);

public:
    SoMFInt32 highlightIndices;
    SoMFInt32 seamIndices;
    SoSFColor highlightColor;
    SoSFBool  elementSelectable;
    SoSFBool  onTopPattern;

    static void initClass();
    SoBrepEdgeSet();

    void setSiblings(std::vector<SoNode*> &&);
    void initBoundingBoxes(const SbVec3f *coords, int numverts);

protected:
    ~SoBrepEdgeSet() override = default;

    void GLRender(SoGLRenderAction *action) override;
    void GLRenderInPath(SoGLRenderAction *action) override;
    void GLRenderBelowPath(SoGLRenderAction * action) override;
    void doAction(SoAction* action) override;
    SoDetail * createLineSegmentDetail(
        SoRayPickAction *action,
        const SoPrimitiveVertex *v1,
        const SoPrimitiveVertex *v2,
        SoPickedPoint *pp) override;
    void getBoundingBox(SoGetBoundingBoxAction * action) override;
    void notify(SoNotList * list) override;
    void rayPick(SoRayPickAction *action) override;

private:
    using SelContext = Gui::SoFCSelectionContextEx;
    using SelContextPtr = Gui::SoFCSelectionContextExPtr;
    
    void glRender(SoGLRenderAction *action, bool inpath);
    void renderHighlight(SoGLRenderAction *action, SelContextPtr);
    void renderSelection(SoGLRenderAction *action, SelContextPtr, bool push=true);
    void _renderSelection(SoGLRenderAction *action, bool checkColor, SbColor color, unsigned pattern, bool push);

    bool isSelected(SelContextPtr ctx);

private:
    SelContextPtr selContext;
    SelContextPtr selContext2;
    Gui::SoFCSelectionCounter selCounter;
    std::vector<SoNode*> siblings;
    std::vector<int> segments;

    BoundBoxRayPick bboxPicker;

    struct SegmentInfo {
        int start;
        int count;
        BoundBoxRayPick picker;
    };
    std::vector<SegmentInfo> bboxMap;
};

} // namespace PartGui


#endif // PARTGUI_SOBREPEDGESET_H

