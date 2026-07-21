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
# include <QApplication>
# include <QMessageBox>
#endif

#include <App/Application.h>
#include <Base/Parameter.h>
#include <Base/Tools.h>

#include "DlgSettings3DViewImp.h"

#include "Application.h"
#include "Document.h"
#include "MainWindow.h"
#include "NavigationStyle.h"
#include "ParamHandler.h"
#include "PrefWidgets.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewParams.h"
#include "Renderer/Renderer.h"
#include "ui_DlgSettings3DView.h"


using namespace Gui::Dialog;
using namespace Gui;
using namespace Render;

/* TRANSLATOR Gui::Dialog::DlgSettings3DViewImp */

/**
 *  Constructs a DlgSettings3DViewImp which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 */
DlgSettings3DViewImp::DlgSettings3DViewImp(QWidget* parent)
    : PreferencePage( parent )
    , ui(new Ui_DlgSettings3DView)
{
    ui->setupUi(this);
    QString tooltip = tr(ViewParams::docRenderCacheMergeCount());
    ui->renderCacheMergeCount->setToolTip(tooltip);
    ui->renderCacheMergeCountLabel->setToolTip(tooltip);
    tooltip = tr(ViewParams::docRenderCacheMergeCountMax());
    ui->renderCacheMergeCountMax->setToolTip(tooltip);
    ui->renderCacheMergeCountMaxLabel->setToolTip(tooltip);
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgSettings3DViewImp::~DlgSettings3DViewImp() = default;

void DlgSettings3DViewImp::saveSettings()
{
    ui->comboAliasing->onSave();
    ui->renderCache->onSave();
    ui->comboRenderer->onSave();
    ui->comboTransparentRender->onSave();
    ui->boxMarkerSize->onSave();
    ui->CheckBox_CornerCoordSystem->onSave();
    ui->SpinBox_CornerCoordSystemSize->onSave();
    ui->CheckBox_ShowAxisCross->onSave();
    ui->CheckBox_ShowFPS->onSave();
    ui->CheckBox_use_SW_OpenGL->onSave();
    ui->CheckBox_useVBO->onSave();
    ui->FloatSpinBox_EyeDistance->onSave();
    ui->checkBoxBacklight->onSave();
    ui->backlightColor->onSave();
    ui->sliderIntensity->onSave();
    ui->radioPerspective->onSave();
    ui->radioOrthographic->onSave();
    ui->CheckBox_ApplyToViews->onSave();
    ui->spinPreselectionDelay->onSave();
    ui->renderCacheMergeCount->onSave();
    ui->renderCacheMergeCountMax->onSave();
    ui->checkBoxEnhancedPick->onSave();
}

void DlgSettings3DViewImp::loadSettings()
{
    ui->CheckBox_CornerCoordSystem->onRestore();
    ui->SpinBox_CornerCoordSystemSize->onRestore();
    ui->CheckBox_ShowAxisCross->onRestore();
    ui->CheckBox_ShowFPS->onRestore();
    ui->CheckBox_use_SW_OpenGL->onRestore();
    ui->CheckBox_useVBO->onRestore();
    ui->FloatSpinBox_EyeDistance->onRestore();
    ui->checkBoxBacklight->onRestore();
    ui->backlightColor->onRestore();
    ui->sliderIntensity->onRestore();
    ui->radioPerspective->onRestore();
    ui->radioOrthographic->onRestore();
    ui->CheckBox_ApplyToViews->onRestore();
    ui->spinPreselectionDelay->onRestore();
    ui->renderCacheMergeCount->onRestore();
    ui->renderCacheMergeCountMax->onRestore();
    ui->comboAliasing->onRestore();
    ui->renderCache->onRestore();

    ui->comboTransparentRender->onRestore();
    ui->checkBoxEnhancedPick->onRestore();

    ui->boxMarkerSize->addItem(tr("5px"), QVariant(5));
    ui->boxMarkerSize->addItem(tr("7px"), QVariant(7));
    ui->boxMarkerSize->addItem(tr("9px"), QVariant(9));
    ui->boxMarkerSize->addItem(tr("11px"), QVariant(11));
    ui->boxMarkerSize->addItem(tr("13px"), QVariant(13));
    ui->boxMarkerSize->addItem(tr("15px"), QVariant(15));
    ui->boxMarkerSize->addItem(tr("20px"), QVariant(20));
    ui->boxMarkerSize->addItem(tr("25px"), QVariant(25));
    ui->boxMarkerSize->addItem(tr("30px"), QVariant(30));
    ui->boxMarkerSize->setCurrentIndex(2); // default value 9px
    ui->boxMarkerSize->onRestore();

    ui->comboRenderer->addItem(tr("Default"), QByteArray("Default"));
    for (auto &type : RendererFactory::types())
        ui->comboRenderer->addItem(tr(type.c_str()), QByteArray(type.c_str()));
    ui->comboRenderer->onRestore();

    auto checkComboRenderer = [this](int v) {
        bool vis = ui->comboRenderer->count() > 1 && v == 3;
        ui->comboRenderer->setVisible(vis);
        ui->labelRenderer->setVisible(vis);
    };
    checkComboRenderer(ui->renderCache->currentIndex());
    QObject::connect(ui->renderCache, QOverload<int>::of(&QComboBox::currentIndexChanged), checkComboRenderer);
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void DlgSettings3DViewImp::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

namespace {

void applyCameraType(ParameterGrp *hGrp)
{
    if (hGrp->GetBool("ApplyCameraTypeToAll", false)) {
        const char *cameraType = hGrp->GetBool("Perspective", false) ?
            "PerspectiveCamera" : "OrthographicCamera";
        for (auto doc : App::GetApplication().getDocuments()) {
            auto gdoc = Application::Instance->getDocument(doc);
            if (!gdoc) continue;
            gdoc->foreachView<MDIView>([cameraType](MDIView *view) {
                view->onMsg(cameraType, nullptr);
            });
        }
    }
}

void applyAntiAlias(ParameterGrp *)
{
    auto activeView = Base::freecad_dynamic_cast<View3DInventor>(
            getMainWindow()->activeWindow());
    std::vector<View3DInventor*> views;
    std::map<View3DInventor*, View3DInventor*> bindings;
    for (auto doc : App::GetApplication().getDocuments()) {
        auto gdoc = Application::Instance->getDocument(doc);
        if (!gdoc) continue;
        gdoc->foreachView<View3DInventor>([&views, &bindings](View3DInventor *view) {
            views.push_back(view);
            if (auto target = view->boundView())
                bindings[view] = target;
        });
    }
    std::map<View3DInventor*, View3DInventor*> viewMap;
    for (auto view : views) {
        // A view driven by an external render backend (bgfx) owns its own
        // multisampled offscreen target; it applies the new sample count in
        // place. Cloning it to obtain a multisampled GL context — as a
        // plain-GL/Coin view requires — would instead tear down and recreate
        // the backend (crashes/deadlocks). Skip the clone for those.
        if (auto viewer = view->getViewer()) {
            if (viewer->applyRendererAntiAliasing())
                continue;
        }
        auto clone = static_cast<View3DInventor*>(
                view->getGuiDocument()->cloneView(view));
        if (!clone)
            continue;
        const char* ppReturn = 0;
        if (view->onMsg("GetCamera", &ppReturn)) {
            std::string sMsg = "SetCamera ";
            sMsg += ppReturn;
            const char** pReturnIgnore=0;
            clone->onMsg(sMsg.c_str(), pReturnIgnore);
        }
        if (view->currentViewMode() == MDIView::Child)
            getMainWindow()->addWindow(clone);
        else
            clone->setCurrentViewMode(view->currentViewMode());
        viewMap[view] = clone;
        view->deleteSelf();
    }

    for (auto &v : bindings) {
        if (auto clone = viewMap[v.first]) {
            if (auto target = viewMap[v.second])
                clone->bindCamera(target->getCamera(), false);
            else
                clone->bindCamera(v.second->getCamera(), false);
        }
    }

    auto it = viewMap.find(activeView);
    if (it != viewMap.end())
        getMainWindow()->setActiveWindow(it->second);
}

} // anonymous namespace


void DlgSettings3DViewImp::attachObserver()
{
    static ParamHandlers handlers;

    auto hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp/Preferences/View");
    handlers.addDelayedHandler(hGrp, {"Orthographic","Perspective","ApplyCameraTypeToAll"}, applyCameraType);
    handlers.addDelayedHandler(hGrp, "AntiAliasing", applyAntiAlias);

    ViewParams::init();
}

#include "moc_DlgSettings3DViewImp.cpp"

