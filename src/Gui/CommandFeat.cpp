/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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


#include "PreCompiled.h"
#ifndef _PreComp_
#endif

#include <App/DocumentObject.h>
#include "Application.h"
#include "CommandT.h"
#include "DockWindowManager.h"
#include "Document.h"
#include "PythonConsole.h"
#include "Selection.h"
#include "SelectionObject.h"
#include "ViewProvider.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderLink.h"
#include "ViewParams.h"

using namespace Gui;



//===========================================================================
// Std_Recompute
//===========================================================================

DEF_STD_CMD(StdCmdFeatRecompute)

StdCmdFeatRecompute::StdCmdFeatRecompute()
  :Command("Std_Recompute")
{
    // setting the
    sGroup        = "File";
    sMenuText     = QT_TR_NOOP("&Recompute");
    sToolTipText  = QT_TR_NOOP("Recompute feature or document");
    sWhatsThis    = "Std_Recompute";
    sStatusTip    = QT_TR_NOOP("Recompute feature or document");
    sPixmap       = "view-refresh";
    sAccel        = "Ctrl+R";
}

void StdCmdFeatRecompute::activated(int iMsg)
{
    Q_UNUSED(iMsg);
}

//===========================================================================
// Std_RandomColor
//===========================================================================

DEF_STD_CMD_A(StdCmdRandomColor)

StdCmdRandomColor::StdCmdRandomColor()
  :Command("Std_RandomColor")
{
    sGroup        = "File";
    sMenuText     = QT_TR_NOOP("Random color");
    sToolTipText  = QT_TR_NOOP("Pick a random color for the selected object.\n"
                               "Note, depending on the object and its 'MapFaceColor'\n"
                               "setting, not all faces may be assigned the color");
    sWhatsThis    = "Std_RandomColor";
    sStatusTip    = sMenuText;
    sPixmap       = "Std_RandomColor";
}

static void inline setRandomColor(const char *name, bool force)
{
    try {
        App::AutoTransaction guard(name, false, true); 
        // get the complete selection
        for (const auto &sel : Selection().getCompleteSelection()) {
            // compute a random color in the HSV space
            SbColor color;
            color.setHSVValue(((float)rand()/RAND_MAX), // [0-1]
                              ((float)rand()/RAND_MAX) * 0.3 + 0.2, // [0.2-0.5]
                              ((float)rand()/RAND_MAX) * 0.35 + 0.55); // [0.55-0.9]

            ViewProvider* view = Application::Instance->getViewProvider(sel.pObject);
            if (auto vpLink = Base::freecad_dynamic_cast<ViewProviderLink>(view)) {
                if(!vpLink->OverrideMaterial.getValue())
                    cmdGuiObjectArgs(sel.pObject, "OverrideMaterial = True");
                cmdGuiObjectArgs(sel.pObject, "ShapeMaterial.DiffuseColor=(%.2f,%.2f,%.2f)",
                                 color[0], color[1], color[2]);
                continue;
            }
            if (Base::freecad_dynamic_cast<App::PropertyColor>(view->getPropertyByName("ShapeColor"))) {
                // get the view provider of the selected object and set the shape color
                cmdGuiObjectArgs(sel.pObject, "ShapeColor=(%.2f,%.2f,%.2f)",
                                 color[0], color[1], color[2]);
            }
            if (force && Base::freecad_dynamic_cast<App::PropertyBool>(view->getPropertyByName("MapFaceColor"))) {
                cmdGuiObjectArgs(sel.pObject, "MapFaceColor = False");
            }
        }
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
}

void StdCmdRandomColor::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    setRandomColor(getName(), false);
}

bool StdCmdRandomColor::isActive()
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_ObjectRandomColor
//===========================================================================

DEF_STD_CMD_A(StdCmdObjectRandomColor)

StdCmdObjectRandomColor::StdCmdObjectRandomColor()
  :Command("Std_ObjectRandomColor")
{
    sGroup        = "File";
    sMenuText     = QT_TR_NOOP("Object random color");
    sToolTipText  = QT_TR_NOOP("Force the whole object to use a uniform randomly picked color");
    sWhatsThis    = "Std_ObjectRandomColor";
    sStatusTip    = sMenuText;
    sPixmap       = "Std_ObjectRandomColor";
}

void StdCmdObjectRandomColor::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    setRandomColor(getName(), true);
}

bool StdCmdObjectRandomColor::isActive(void)
{
    return (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_GroupRandomColor
//===========================================================================

class StdCmdGroupRandomColor : public GroupCommand
{
public:
    StdCmdGroupRandomColor()
        :GroupCommand("Std_GroupRandomColor")
    {
        sGroup        = "Standard-View";
        sMenuText     = QT_TR_NOOP("Random color");
        sToolTipText  = QT_TR_NOOP("Actions for setting random color");
        sWhatsThis    = "Std_GroupRandomColor";
        sStatusTip    = sToolTipText;
        eType         = 0;
        bCanLog       = false;

        addCommand(new StdCmdRandomColor());
        addCommand(new StdCmdObjectRandomColor());
    };
    virtual const char* className() const {return "StdCmdGroupRandomColor";}
};

//===========================================================================
// Std_SendToPythonConsole
//===========================================================================

DEF_STD_CMD_A(StdCmdSendToPythonConsole)

StdCmdSendToPythonConsole::StdCmdSendToPythonConsole()
  :Command("Std_SendToPythonConsole")
{
    // setting the
    sGroup        = "Edit";
    sMenuText     = QT_TR_NOOP("&Send to Python Console");
    sToolTipText  = QT_TR_NOOP("Sends the selected object to the Python console");
    sWhatsThis    = "Std_SendToPythonConsole";
    sStatusTip    = QT_TR_NOOP("Sends the selected object to the Python console");
    sPixmap       = "applications-python";
    sAccel        = "Ctrl+Shift+P";
}

bool StdCmdSendToPythonConsole::isActive()
{
    //active only if either 1 object is selected or multiple subobjects from the same object
    return Gui::Selection().getSelectionEx().size() == 1;
}

void StdCmdSendToPythonConsole::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const std::vector<Gui::SelectionObject> &sels = Gui::Selection().getSelectionEx("*", App::DocumentObject::getClassTypeId(),
                                                                                    ResolveMode::OldStyleElement, false);
    if (sels.empty())
        return;
    const App::DocumentObject *obj = sels[0].getObject();

    if (!obj)
        return;
    QString docname = QString::fromUtf8(obj->getDocument()->getName());
    QString objname = QString::fromUtf8(obj->getNameInDocument());
    try {
        // clear variables from previous run, if any
        Gui::Command::runCommand(Gui::Command::Gui, "try:\n    del(doc,lnk,obj,shp,sub,subs)\nexcept Exception:\n    pass\n");
        QString cmd = QStringLiteral("doc = App.getDocument(\"%1\")").arg(docname);
        Gui::Command::runCommand(Gui::Command::Gui,cmd.toUtf8());
        //support links
        auto linked = obj->getLinkedObject();
        if (linked && linked != obj) {
            cmd = QStringLiteral("lnk = doc.getObject(\"%1\")").arg(objname);
            Gui::Command::runCommand(Gui::Command::Gui,cmd.toUtf8());
            cmd = QStringLiteral("obj = lnk.getLinkedObject()");
            Gui::Command::runCommand(Gui::Command::Gui,cmd.toUtf8());
            obj = linked;
        } else {
            cmd = QStringLiteral("obj = doc.getObject(\"%1\")").arg(objname);
            Gui::Command::runCommand(Gui::Command::Gui,cmd.toUtf8());
        }
        if (obj->isDerivedFrom<App::GeoFeature>()) {
            const auto geoObj = static_cast<const App::GeoFeature*>(obj);
            const App::PropertyGeometry* geo = geoObj->getPropertyOfGeometry();
            if (geo){
                cmd = QStringLiteral("shp = obj.") + QString::fromUtf8(geo->getName()); //"Shape", "Mesh", "Points", etc.
                Gui::Command::runCommand(Gui::Command::Gui, cmd.toUtf8());
                if (sels[0].hasSubNames()) {
                    std::vector<std::string> subnames = sels[0].getSubNames();
                    QString subname = QString::fromUtf8(subnames[0].c_str());
                    cmd = QStringLiteral("sub = obj.getSubObject(\"%1\")").arg(subname);
                    Gui::Command::runCommand(Gui::Command::Gui,cmd.toUtf8());
                    if (subnames.size() > 1) {
                        std::ostringstream strm;
                        strm << "subs = [";
                        for (const auto & subname : subnames) {
                            strm << "obj.getSubObject(\"" << subname << "\"),";
                        }
                        strm << "]";
                        Gui::Command::runCommand(Gui::Command::Gui, strm.str().c_str());
                    }
                }
            }
        }
        //show the python console if it's not already visible, and set the keyboard focus to it
        QWidget* pc = DockWindowManager::instance()->getDockWindow("Python console");
        auto pcPython = qobject_cast<PythonConsole*>(pc);
        if (pcPython) {
            DockWindowManager::instance()->activate(pcPython);
            pcPython->setFocus();
        }
    }
    catch (const Base::Exception& e) {
        e.ReportException();
    }

}

//===========================================================================
// Std_RenameActiveObject
//===========================================================================

DEF_STD_CMD_A(StdCmdRenameActiveObject)

StdCmdRenameActiveObject::StdCmdRenameActiveObject()
  :Command("Std_RenameActiveObject")
{
    // setting the
    sGroup        = "Edit";
    sMenuText     = QT_TR_NOOP("Rename new object");
    sToolTipText  = QT_TR_NOOP("Rename the newly created object");
    sWhatsThis    = "Std_RenameActiveObject";
    sStatusTip    = sToolTipText;
    sAccel        = "Shift+F2";
}

bool StdCmdRenameActiveObject::isActive(void)
{
    auto doc = App::GetApplication().getActiveDocument();
    return doc && doc->getActiveObject();
}

void StdCmdRenameActiveObject::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    auto doc = App::GetApplication().getActiveDocument();
    auto tree = TreeWidget::instance();
    if (!tree || !tree->isVisible() || !doc || !doc->getActiveObject())
        return;

    Gui::Selection().selStackPush();
    Gui::Selection().clearSelection();
    Gui::Selection().addSelection(doc->getActiveObject());
    Gui::Selection().selStackPush();
    TreeWidget::scrollItemToTop();
    auto items = tree->selectedItems();
    if (items.size() == 1)
        tree->editItem(items[0]);
}


namespace Gui {

void CreateFeatCommands()
{
    CommandManager &rcCmdMgr = Application::Instance->commandManager();

    rcCmdMgr.addCommand(new StdCmdGroupRandomColor());
    rcCmdMgr.addCommand(new StdCmdSendToPythonConsole());
    rcCmdMgr.addCommand(new StdCmdRenameActiveObject());
}

} // namespace Gui
