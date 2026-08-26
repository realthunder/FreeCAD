/***************************************************************************
 *   Copyright (c) 2024 Benjamin Braestrup Sayoc <benj5378@outlook.com>    *
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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QMessageBox>
#endif

#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection.h>

#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/DrawViewPart.h>

#include "DrawGuiUtil.h"
#include "QGIEdge.h"
#include "QGIVertex.h"
#include "QGIView.h"
#include "ViewProviderViewPart.h"


using namespace TechDrawGui;
using namespace TechDraw;

namespace
{

void incorrectSelection()
{
    QMessageBox::warning(Gui::getMainWindow(),
                         QObject::tr("Incorrect Selection"),
                         QObject::tr("You must select 2 vertices or 1 edge\n"));
}

//! the sub-element names selected on obj.  the selection is searched for obj
//! rather than assuming it is the first entry, because a Link selection
//! resolves to an object that need not be the one at index 0.
std::vector<std::string> subNamesOf(App::DocumentObject* obj)
{
    for (const Gui::SelectionObject& selObj : Gui::Command::getSelection().getSelectionEx()) {
        if (selObj.getObject() == obj) {
            return selObj.getSubNames();
        }
    }
    return {};
}

//! rotate the single selected view so that the selected geometry -- two
//! vertices, or one edge -- lines up with direction.
void alignByRotation(const Base::Vector2d& direction)
{
    std::vector<DrawViewPart*> dvps =
        Gui::Command::getSelection().getObjectsOfType<DrawViewPart>();
    if (dvps.size() != 1) {
        incorrectSelection();
        return;
    }
    DrawViewPart* dvp = dvps.front();

    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(dvp->getDocument());
    if (!guiDoc) {
        return;
    }

    auto vpdvp = dynamic_cast<ViewProviderViewPart*>(guiDoc->getViewProvider(dvp));
    if (!vpdvp) {
        return;
    }

    QGIView* view = vpdvp->getQView();
    if (!view) {
        return;
    }

    std::vector<std::string> subNames = subNamesOf(dvp);
    if (subNames.empty() || !DrawUtil::isGeomTypeConsistent(subNames)) {
        incorrectSelection();
        return;
    }

    std::vector<int> subIndexes = DrawUtil::getIndexFromName(subNames);
    std::string subType = DrawUtil::getGeomTypeFromName(subNames.front());

    if (subType == "Vertex") {
        std::vector<QGIVertex*> vertexes = view->getObjects<QGIVertex*>(subIndexes);
        if (vertexes.size() == 2) {
            Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Align Vertices"));
            DrawGuiUtil::rotateToAlign(vertexes.front(), vertexes.back(), direction);
            Gui::Command::commitCommand();
            dvp->recomputeFeature();
            return;
        }
    }
    else if (subType == "Edge") {
        std::vector<QGIEdge*> edges = view->getObjects<QGIEdge*>(subIndexes);
        if (edges.size() == 1) {
            Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Align Edge"));
            DrawGuiUtil::rotateToAlign(edges.front(), direction);
            Gui::Command::commitCommand();
            dvp->recomputeFeature();
            return;
        }
    }

    incorrectSelection();
}

}// anonymous namespace


//===========================================================================
// TechDraw_AlignVertexesVertically
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawAlignVertexesVertically)

CmdTechDrawAlignVertexesVertically::CmdTechDrawAlignVertexesVertically()
    : Command("TechDraw_AlignVertexesVertically")
{
    sAppModule = "TechDraw";
    sGroup = QT_TR_NOOP("TechDraw");
    sMenuText = QT_TR_NOOP("Align Vertices/Edge Vertically");
    sToolTipText = QT_TR_NOOP("Aligns the selected vertices or edge vertically by rotating the view");
    sWhatsThis = "TechDraw_AlignVertexesVertically";
    sStatusTip = sToolTipText;
}

void CmdTechDrawAlignVertexesVertically::activated(int iMsg)
{
    Q_UNUSED(iMsg)

    alignByRotation(Base::Vector2d(0.0, 1.0));
}

bool CmdTechDrawAlignVertexesVertically::isActive()
{
    bool havePage = DrawGuiUtil::needPage(this);
    bool haveView = DrawGuiUtil::needView(this, false);
    return (havePage && haveView);
}


//===========================================================================
// TechDraw_AlignVertexesHorizontally
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawAlignVertexesHorizontally)

CmdTechDrawAlignVertexesHorizontally::CmdTechDrawAlignVertexesHorizontally()
    : Command("TechDraw_AlignVertexesHorizontally")
{
    sAppModule = "TechDraw";
    sGroup = QT_TR_NOOP("TechDraw");
    sMenuText = QT_TR_NOOP("Align Vertices/Edge Horizontally");
    sToolTipText =
        QT_TR_NOOP("Aligns the selected vertices or edge horizontally by rotating the view");
    sWhatsThis = "TechDraw_AlignVertexesHorizontally";
    sStatusTip = sToolTipText;
}

void CmdTechDrawAlignVertexesHorizontally::activated(int iMsg)
{
    Q_UNUSED(iMsg)

    alignByRotation(Base::Vector2d(1.0, 0.0));
}

bool CmdTechDrawAlignVertexesHorizontally::isActive()
{
    bool havePage = DrawGuiUtil::needPage(this);
    bool haveView = DrawGuiUtil::needView(this, false);
    return (havePage && haveView);
}


void CreateTechDrawCommandsAlign()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdTechDrawAlignVertexesVertically());
    rcCmdMgr.addCommand(new CmdTechDrawAlignVertexesHorizontally());
}
