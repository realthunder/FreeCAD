// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPointer>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <Mod/Material/App/PropertyMaterial.h>
#include <Mod/Material/App/MaterialClipboard.h>
#include <Mod/Material/App/ShaderGraph.h>

#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/ViewProvider.h>
#include <Gui/Control.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Gui/ViewProviderGeometryObject.h>

#include "DlgDisplayPropertiesImp.h"
#include "DlgInspectAppearance.h"
#include "DlgInspectMaterial.h"
#include "DlgMaterialImp.h"
#include "MaterialSave.h"
#include "MaterialsEditor.h"
#include "ModelSelect.h"
#include "TaskMigrateExternal.h"


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//===========================================================================
// Material_Edit
//===========================================================================
DEF_STD_CMD_A(CmdMaterialEdit)

CmdMaterialEdit::CmdMaterialEdit()
    : Command("Material_Edit")
{
    sAppModule = "Material";
    sGroup = QT_TR_NOOP("Material");
    sMenuText = QT_TR_NOOP("Edit");
    sToolTipText = QT_TR_NOOP("Edits material properties");
    sWhatsThis = "Material_Edit";
    sStatusTip = sToolTipText;
    sPixmap = "Material_Edit";
}

void CmdMaterialEdit::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    static QPointer<QDialog> dlg = nullptr;
    if (!dlg) {
        dlg = new MatGui::MaterialsEditor(Gui::getMainWindow());
    }
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

bool CmdMaterialEdit::isActive()
{
    // return (hasActiveDocument() && !Gui::Control().activeDialog());
    return true;
}

//===========================================================================
// Std_SetAppearance
//===========================================================================
DEF_STD_CMD_A(StdCmdSetAppearance)

StdCmdSetAppearance::StdCmdSetAppearance()
    : Command("Std_SetAppearance")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("&Appearance");
    sToolTipText = QT_TR_NOOP("Sets the display properties of the selected object");
    sWhatsThis = "Std_SetAppearance";
    sStatusTip = QT_TR_NOOP("Sets the display properties of the selected object");
    sPixmap = "Std_SetAppearance";
    sAccel = "Ctrl+D";
    eType = Alter3DView;
}

void StdCmdSetAppearance::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new MatGui::TaskDisplayProperties());
}

bool StdCmdSetAppearance::isActive()
{
    return (Gui::Control().activeDialog() == nullptr) && (Gui::Selection().size() != 0);
}

//===========================================================================
// Std_SetMaterial
//===========================================================================
DEF_STD_CMD_A(StdCmdSetMaterial)

StdCmdSetMaterial::StdCmdSetMaterial()
    : Command("Std_SetMaterial")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("&Material");
    sToolTipText = QT_TR_NOOP("Sets the material of the selected object");
    sWhatsThis = "Std_SetMaterial";
    sStatusTip = QT_TR_NOOP("Sets the material of the selected object");
    sPixmap = "Material_Edit";
    // sAccel        = "Ctrl+D";
    // eType = Alter3DView;
}

void StdCmdSetMaterial::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new MatGui::TaskMaterial());
}

bool StdCmdSetMaterial::isActive()
{
    return (Gui::Control().activeDialog() == nullptr) && (Gui::Selection().size() != 0);
}

//===========================================================================
// Materials_InspectAppearance
//===========================================================================
DEF_STD_CMD_A(CmdInspectAppearance)

CmdInspectAppearance::CmdInspectAppearance()
    : Command("Materials_InspectAppearance")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("Inspect Appearance");
    sToolTipText = QT_TR_NOOP("Inspects the appearance properties of the selected object");
    sWhatsThis = "Materials_InspectAppearance";
    sStatusTip = QT_TR_NOOP("Inspect the appearance properties of the selected object");
    // sPixmap = "Material_Edit";
}

void CmdInspectAppearance::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new MatGui::TaskInspectAppearance());
}

bool CmdInspectAppearance::isActive()
{
    return (Gui::Control().activeDialog() == nullptr);
}

//===========================================================================
// Materials_InspectMaterial
//===========================================================================
DEF_STD_CMD_A(CmdInspectMaterial)

CmdInspectMaterial::CmdInspectMaterial()
    : Command("Materials_InspectMaterial")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("Inspect Material");
    sToolTipText = QT_TR_NOOP("Inspects the material properties of the selected object");
    sWhatsThis = "Materials_InspectMaterial";
    sStatusTip = QT_TR_NOOP("Inspect the material properties of the selected object");
    // sPixmap = "Material_Edit";
}

void CmdInspectMaterial::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new MatGui::TaskInspectMaterial());
}

bool CmdInspectMaterial::isActive()
{
    return (Gui::Control().activeDialog() == nullptr);
}

//===========================================================================
// Materials_MigrateToDatabase
//===========================================================================

#if defined(BUILD_MATERIAL_EXTERNAL)
DEF_STD_CMD_A(CmdMigrateToExternal)

CmdMigrateToExternal::CmdMigrateToExternal()
    : Command("Materials_MigrateToExternal")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("Migrate");
    sToolTipText = QT_TR_NOOP("Migrates the materials to the external materials manager");
    sWhatsThis = "Materials_MigrateToDatabase";
    sStatusTip = QT_TR_NOOP("Migrate existing materials to the external materials manager");
    // sPixmap = "Materials_Edit";
}

void CmdMigrateToExternal::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    MatGui::TaskMigrateExternal* dlg = new MatGui::TaskMigrateExternal();
    Gui::Control().showDialog(dlg);
}

bool CmdMigrateToExternal::isActive()
{
    return true;
}
#endif

//===========================================================================
// Material_UpdateFromLibrary / Material_SaveToLibrary
//===========================================================================

namespace
{

/// Every material property on an object. Only Part::Feature has one today.
std::vector<Materials::PropertyMaterial*> materialProperties(App::DocumentObject* object)
{
    std::vector<Materials::PropertyMaterial*> found;
    std::vector<App::Property*> properties;
    object->getPropertyList(properties);
    for (auto property : properties) {
        if (property->isDerivedFrom<Materials::PropertyMaterial>()) {
            found.push_back(static_cast<Materials::PropertyMaterial*>(property));
        }
    }
    return found;
}

std::vector<Materials::PropertyMaterial*> selectedMaterials()
{
    std::vector<Materials::PropertyMaterial*> found;
    for (auto object : Gui::Selection().getObjectsOfType<App::DocumentObject>()) {
        auto properties = materialProperties(object);
        found.insert(found.end(), properties.begin(), properties.end());
    }
    return found;
}

}  // namespace

DEF_STD_CMD_A(CmdMaterialUpdateFromLibrary)

CmdMaterialUpdateFromLibrary::CmdMaterialUpdateFromLibrary()
    : Command("Material_UpdateFromLibrary")
{
    sAppModule = "Material";
    sGroup = QT_TR_NOOP("Material");
    sMenuText = QT_TR_NOOP("Update Material From Library");
    sToolTipText =
        QT_TR_NOOP("Replaces the material stored in the document with the library's current one");
    sWhatsThis = "Material_UpdateFromLibrary";
    sStatusTip = sToolTipText;
}

void CmdMaterialUpdateFromLibrary::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    openCommand(QT_TRANSLATE_NOOP("Command", "Update material from library"));
    int updated = 0;
    for (auto property : selectedMaterials()) {
        if (property->updateFromLibrary()) {
            ++updated;
        }
    }
    if (updated == 0) {
        abortCommand();
        return;
    }
    commitCommand();
    updateActive();
}

bool CmdMaterialUpdateFromLibrary::isActive()
{
    // Offered only where it would do something. A library that has moved on is
    // the only thing there is to take.
    for (auto property : selectedMaterials()) {
        if (property->libraryStatus() == Materials::PropertyMaterial::LibraryStatus::Diverged) {
            return true;
        }
    }
    return false;
}

//===========================================================================

DEF_STD_CMD_A(CmdMaterialSaveToLibrary)

CmdMaterialSaveToLibrary::CmdMaterialSaveToLibrary()
    : Command("Material_SaveToLibrary")
{
    sAppModule = "Material";
    sGroup = QT_TR_NOOP("Material");
    sMenuText = QT_TR_NOOP("Save Material To Library");
    sToolTipText = QT_TR_NOOP("Writes the material stored in the document back to the library");
    sWhatsThis = "Material_SaveToLibrary";
    sStatusTip = sToolTipText;
}

void CmdMaterialSaveToLibrary::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // An object whose materialized shader graph was edited saves the EDIT:
    // a new card inheriting from the one it wears, carrying the text as it
    // is now (docs/MaterialStorage.md 17.12). The object then wears that
    // card and the binding comes off, since the card draws the same.
    std::set<Materials::PropertyMaterial*> edited;
    for (auto property : selectedMaterials()) {
        auto owner = dynamic_cast<App::DocumentObject*>(property->getContainer());
        if (!Materials::ShaderGraph::edited(owner)) {
            continue;
        }
        edited.insert(property);
        auto card = Materials::ShaderGraph::cardFromEdit(owner, *property);
        if (!card) {
            continue;
        }
        MatGui::MaterialSave dialog(card, Gui::getMainWindow());
        if (dialog.exec() != QDialog::Accepted) {
            continue;
        }
        openCommand(QT_TRANSLATE_NOOP("Command", "Save shader graph to library"));
        property->setValue(*card);
        Materials::ShaderGraph::revert(owner);
        commitCommand();
    }

    // By content: the same card assigned to twenty objects is one thing to
    // write, and one question to ask if it needs a home.
    std::map<std::string, std::vector<Materials::PropertyMaterial*>> byContent;
    for (auto property : selectedMaterials()) {
        if (edited.count(property)) {
            continue;
        }
        if (property->libraryStatus() != Materials::PropertyMaterial::LibraryStatus::NoCard
            && !property->isUnresolved()) {
            byContent[property->getContentHash()].push_back(property);
        }
    }

    for (auto& entry : byContent) {
        auto& properties = entry.second;
        if (properties.front()->saveToLibrary()) {
            // Written in place, over the card it came from. Every property
            // holding this content now matches the library.
            continue;
        }

        // No card to write over, or a read only library. Where it goes is the
        // user's answer, and this is the dialog that already asks.
        auto card = std::make_shared<Materials::Material>(properties.front()->cardForLibrary());
        MatGui::MaterialSave dialog(card, Gui::getMainWindow());
        if (dialog.exec() != QDialog::Accepted) {
            continue;
        }
        // The dialog wrote the card somewhere and may have given it a new
        // uuid. Re-anchor, or the document would still point at the card it
        // came from and this would have to be answered again next time.
        openCommand(QT_TRANSLATE_NOOP("Command", "Save material to library"));
        for (auto property : properties) {
            property->setValue(*card);
        }
        commitCommand();
    }
}

bool CmdMaterialSaveToLibrary::isActive()
{
    for (auto property : selectedMaterials()) {
        if (property->libraryStatus() != Materials::PropertyMaterial::LibraryStatus::NoCard
            && !property->isUnresolved()) {
            return true;
        }
        if (Materials::ShaderGraph::edited(
                dynamic_cast<App::DocumentObject*>(property->getContainer()))) {
            return true;
        }
    }
    return false;
}

//===========================================================================
// Material_Copy / Material_Paste
//===========================================================================

namespace
{
// The look an object draws with lives on its view provider
App::PropertyAppearanceList* lookOf(App::DocumentObject* object)
{
    auto vp = Gui::Application::Instance->getViewProvider(object);
    return vp ? dynamic_cast<App::PropertyAppearanceList*>(vp->getPropertyByName("ShapeAppearance"))
              : nullptr;
}

Materials::PropertyMaterial* cardOf(App::DocumentObject* object)
{
    return dynamic_cast<Materials::PropertyMaterial*>(object->getPropertyByName("ShapeMaterial"));
}

// "Face7" of a sub-name that may be a path ("Group.Link.Face7") -> 6
int faceIndexOf(const std::string& subname)
{
    const auto dot = subname.rfind('.');
    const std::string element = dot == std::string::npos ? subname : subname.substr(dot + 1);
    if (element.compare(0, 4, "Face") != 0) {
        return -1;
    }
    try {
        return std::stoi(element.substr(4)) - 1;
    }
    catch (const std::exception&) {
        return -1;
    }
}
}  // namespace

DEF_STD_CMD_A(CmdMaterialCopy)

CmdMaterialCopy::CmdMaterialCopy()
    : Command("Material_Copy")
{
    sAppModule = "Material";
    sGroup = QT_TR_NOOP("Material");
    sMenuText = QT_TR_NOOP("Copy Material");
    sToolTipText = QT_TR_NOOP("Copies the object's material card and look, with every file they "
                              "refer to, so they can be pasted onto other objects, in other "
                              "documents too");
    sWhatsThis = "Material_Copy";
    sStatusTip = sToolTipText;
}

void CmdMaterialCopy::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // The first object with something to carry: one material goes on the
    // clipboard, however many objects are selected
    for (auto object : Gui::Selection().getObjectsOfType<App::DocumentObject>()) {
        const std::string data =
            Materials::Clipboard::pack(cardOf(object), lookOf(object), *object->getDocument());
        if (data.empty()) {
            continue;
        }
        auto mime = new QMimeData();
        mime->setData(QString::fromLatin1(Materials::Clipboard::mimeType()),
                      QByteArray(data.data(), static_cast<int>(data.size())));
        QApplication::clipboard()->setMimeData(mime);
        return;
    }
}

bool CmdMaterialCopy::isActive()
{
    for (auto object : Gui::Selection().getObjectsOfType<App::DocumentObject>()) {
        if (auto card = cardOf(object)) {
            if (!card->isUnresolved()
                && card->libraryStatus() != Materials::PropertyMaterial::LibraryStatus::NoCard) {
                return true;
            }
        }
        if (auto look = lookOf(object)) {
            if (look->getSize()) {
                return true;
            }
        }
    }
    return false;
}

DEF_STD_CMD_A(CmdMaterialPaste)

CmdMaterialPaste::CmdMaterialPaste()
    : Command("Material_Paste")
{
    sAppModule = "Material";
    sGroup = QT_TR_NOOP("Material");
    sMenuText = QT_TR_NOOP("Paste Material");
    sToolTipText = QT_TR_NOOP("Gives the selected objects the copied material card and look; "
                              "selected faces take the look as a per-face override");
    sWhatsThis = "Material_Paste";
    sStatusTip = sToolTipText;
}

void CmdMaterialPaste::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    const QString type = QString::fromLatin1(Materials::Clipboard::mimeType());
    if (!mime || !mime->hasFormat(type)) {
        return;
    }
    const QByteArray bytes = mime->data(type);
    const std::string data(bytes.constData(), static_cast<std::size_t>(bytes.size()));

    openCommand(QT_TRANSLATE_NOOP("Command", "Paste material"));
    bool applied = false;
    for (const auto& selection : Gui::Selection().getSelectionEx(nullptr, App::DocumentObject::getClassTypeId(), Gui::ResolveMode::NoResolve)) {
        // The selection hands out const objects; a paste is a write
        auto object = const_cast<App::DocumentObject*>(selection.getObject());
        if (!object || !object->getDocument()) {
            continue;
        }
        std::vector<int> faces;
        for (const auto& subname : selection.getSubNames()) {
            const int face = faceIndexOf(subname);
            if (face >= 0) {
                faces.push_back(face);
            }
        }
        // A face selection pastes the look onto those faces only; the
        // card is the object's and is not touched by a face paste
        auto card = faces.empty() ? cardOf(object) : nullptr;
        applied = Materials::Clipboard::apply(data, card, lookOf(object), faces, *object->getDocument())
            || applied;
    }
    if (applied) {
        commitCommand();
    }
    else {
        abortCommand();
    }
}

bool CmdMaterialPaste::isActive()
{
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QString::fromLatin1(Materials::Clipboard::mimeType()))) {
        return false;
    }
    return !Gui::Selection().getObjectsOfType<App::DocumentObject>().empty();
}

//===========================================================================
// Material_ResetAppearance
//===========================================================================

namespace
{

/// Every selected view provider that could take its card's look again.
std::vector<Gui::ViewProviderGeometryObject*> resettableSelection()
{
    std::vector<Gui::ViewProviderGeometryObject*> found;
    for (auto object : Gui::Selection().getObjectsOfType<App::DocumentObject>()) {
        auto* vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(
            Gui::Application::Instance->getViewProvider(object));
        if (vp && vp->canResetAppearanceToMaterial()) {
            found.push_back(vp);
        }
    }
    return found;
}

}  // namespace

DEF_STD_CMD_A(CmdMaterialResetAppearance)

CmdMaterialResetAppearance::CmdMaterialResetAppearance()
    : Command("Material_ResetAppearance")
{
    sAppModule = "Material";
    sGroup = QT_TR_NOOP("Material");
    sMenuText = QT_TR_NOOP("Reset Appearance To Material");
    sToolTipText =
        QT_TR_NOOP("Takes the look from the object's material card again, and keeps taking it");
    sWhatsThis = "Material_ResetAppearance";
    sStatusTip = sToolTipText;
    eType = Alter3DView;
}

void CmdMaterialResetAppearance::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    openCommand(QT_TRANSLATE_NOOP("Command", "Reset appearance to material"));
    int reset = 0;
    for (auto vp : resettableSelection()) {
        if (vp->resetAppearanceToMaterial()) {
            ++reset;
        }
    }
    if (reset == 0) {
        abortCommand();
        return;
    }
    commitCommand();
    updateActive();
}

bool CmdMaterialResetAppearance::isActive()
{
    // Offered only while it applies -- the rule the sync commands follow
    // (docs/MaterialStorage.md 13.5, 15.5). An object still following its
    // card has nowhere to go back to.
    return !resettableSelection().empty();
}

//---------------------------------------------------------------

void CreateMaterialCommands()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdMaterialEdit());
    rcCmdMgr.addCommand(new StdCmdSetAppearance());
    rcCmdMgr.addCommand(new StdCmdSetMaterial());
    rcCmdMgr.addCommand(new CmdInspectAppearance());
    rcCmdMgr.addCommand(new CmdInspectMaterial());
    rcCmdMgr.addCommand(new CmdMaterialUpdateFromLibrary());
    rcCmdMgr.addCommand(new CmdMaterialSaveToLibrary());
    rcCmdMgr.addCommand(new CmdMaterialResetAppearance());
    rcCmdMgr.addCommand(new CmdMaterialCopy());
    rcCmdMgr.addCommand(new CmdMaterialPaste());
#if defined(BUILD_MATERIAL_EXTERNAL)
    rcCmdMgr.addCommand(new CmdMigrateToExternal());
#endif
}
