// SPDX-License-Identifier: LGPL-2.1-or-later

 /****************************************************************************
  *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
  *   Copyright (c) 2023 FreeCAD Project Association                         *
  *                                                                          *
  *   This file is part of FreeCAD.                                          *
  *                                                                          *
  *   FreeCAD is free software: you can redistribute it and/or modify it     *
  *   under the terms of the GNU Lesser General Public License as            *
  *   published by the Free Software Foundation, either version 2.1 of the   *
  *   License, or (at your option) any later version.                        *
  *                                                                          *
  *   FreeCAD is distributed in the hope that it will be useful, but         *
  *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
  *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
  *   Lesser General Public License for more details.                        *
  *                                                                          *
  *   You should have received a copy of the GNU Lesser General Public       *
  *   License along with FreeCAD. If not, see                                *
  *   <https://www.gnu.org/licenses/>.                                       *
  *                                                                          *
  ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <cmath>
# include <limits>
# include <QApplication>
# include <QLocale>
# include <QStyleFactory>
# include <QTextStream>
# include <QTimer>
#endif

#include <App/Document.h>
#include <Base/Parameter.h>
#include <Base/UnitsApi.h>

#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/DockWindowManager.h>
#include <Gui/Document.h>
#include <Gui/DlgCreateNewPreferencePackImp.h>
#include <Gui/DlgPreferencesImp.h>
#include <Gui/DlgPreferencePackManagementImp.h>
#include <Gui/DlgRevertToBackupConfigImp.h>
#include <Gui/MainWindow.h>
#include <Gui/PrefWidgets.h>
#include <Gui/PythonConsole.h>
#include <Gui/ToolBarManager.h>
#include <Gui/TreeParams.h>
#include <Gui/ViewParams.h>
#include <Gui/OverlayManager.h>
#include <Gui/ParamHandler.h>
#include <Gui/PreferencePackManager.h>
#include <Gui/Language/Translator.h>

#include "DlgSettingsGeneral.h"
#include "ui_DlgSettingsGeneral.h"

using namespace Base;


using namespace Gui;
using namespace Gui::Dialog;

static DlgSettingsGeneral *_Instance;

/* TRANSLATOR Gui::Dialog::DlgSettingsGeneral */

/**
 *  Constructs a DlgSettingsGeneral which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgSettingsGeneral::DlgSettingsGeneral( QWidget* parent )
  : PreferencePage(parent)
  , ui(new Ui_DlgSettingsGeneral)
{
    ui->setupUi(this);

    connect(ui->comboBox_UnitSystem, qOverload<int>(&QComboBox::currentIndexChanged), this, &DlgSettingsGeneral::onUnitSystemIndexChanged);
    ui->spinBoxDecimals->setMaximum(std::numeric_limits<double>::digits10 + 1);

    int num = static_cast<int>(Base::UnitSystem::NumUnitSystemTypes);
    for (int i = 0; i < num; i++) {
        QString item = Base::UnitsApi::getDescription(static_cast<Base::UnitSystem>(i));
        ui->comboBox_UnitSystem->addItem(item, i);
    }

    // Enable/disable the fractional inch option depending on system
    if (UnitsApi::getSchema() == UnitSystem::ImperialBuilding)
    {
        ui->comboBox_FracInch->setVisible(true);
        ui->fractionalInchLabel->setVisible(true);
    }
    else
    {
        ui->comboBox_FracInch->setVisible(false);
        ui->fractionalInchLabel->setVisible(false);
    }

    _Instance = this;
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgSettingsGeneral::~DlgSettingsGeneral()
{
    _Instance = nullptr;
}

/** Sets the size of the recent file list from the user parameters.
 * @see RecentFilesAction
 * @see StdCmdRecentFiles
 */
void DlgSettingsGeneral::setRecentFileSize()
{
    RecentFilesAction *recent = getMainWindow()->findChild<RecentFilesAction *>
        (QStringLiteral("recentFiles"));
    if (recent) {
        ParameterGrp::handle hGrp = WindowParameter::getDefaultParameter()->GetGroup("RecentFiles");
        recent->resizeList(hGrp->GetInt("RecentFiles", 4));
    }
}

static void saveTreeMode(int value)
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/DockWindows");
    bool treeView=false, propertyView=false, comboView=true;
    switch(value) {
    case 1:
        treeView = true;
        propertyView = true;
        comboView = false;
        break;
    case 2:
        treeView = true;
        comboView = true;
        propertyView = true;
        break;
    }

    if(propertyView != hGrp->GetGroup("PropertyView")->GetBool("Enabled",false)
            || treeView != hGrp->GetGroup("TreeView")->GetBool("Enabled",false)
            || comboView != hGrp->GetGroup("ComboView")->GetBool("Enabled",true))
    {
        hGrp->GetGroup("ComboView")->SetBool("Enabled",comboView);
        hGrp->GetGroup("TreeView")->SetBool("Enabled",treeView);
        hGrp->GetGroup("PropertyView")->SetBool("Enabled",propertyView);
    }
}

void DlgSettingsGeneral::saveSettings()
{
    // must be done as very first because we create a new instance of NavigatorStyle
    // where we set some attributes afterwards
    int FracInch;  // minimum fractional inch to display
    int viewSystemIndex; // currently selected View System (unit system)

    ParameterGrp::handle hGrpu = App::GetApplication().GetParameterGroupByPath
    ("User parameter:BaseApp/Preferences/Units");
    hGrpu->SetInt("UserSchema", ui->comboBox_UnitSystem->currentIndex());
    hGrpu->SetInt("Decimals", ui->spinBoxDecimals->value());
    hGrpu->SetBool("IgnoreProjectSchema", ui->checkBox_projectUnitSystemIgnore->isChecked());

    // Set actual value
    Base::UnitsApi::setDecimals(ui->spinBoxDecimals->value());

    // Convert the combobox index to the its integer denominator. Currently
    // with 1/2, 1/4, through 1/128, this little equation directly computes the
    // denominator given the combobox integer.
    //
    // The inverse conversion is done when loaded. That way only one thing (the
    // numerical fractional inch value) needs to be stored.
    FracInch = std::pow(2, ui->comboBox_FracInch->currentIndex() + 1);
    hGrpu->SetInt("FracInch", FracInch);

    // Set the actual format value
    Base::QuantityFormat::setDefaultDenominator(FracInch);

    // Set and save the Unit System
    if ( ui->checkBox_projectUnitSystemIgnore->isChecked() ) {
        viewSystemIndex = ui->comboBox_UnitSystem->currentIndex();
        UnitsApi::setSchema(static_cast<UnitSystem>(viewSystemIndex));
    } else {
        App::Document* doc = App::GetApplication().getActiveDocument();
        if ( doc != nullptr ) {
            UnitsApi::setSchema(static_cast<UnitSystem>(doc->UnitSystem.getValue()));
        }
    }

    setRecentFileSize();

    ui->AutoApply->onSave();
    ui->SaveParameter->onSave();
    ui->Languages->onSave();
    ui->toolbarArea->onSave();
    ui->globalToolbarArea->onSave();
    ui->treeIconSize->onSave();
    ui->treeFontSize->onSave();
    ui->treeItemSpacing->onSave();
    ui->appFontSize->onSave();
    ui->RecentFiles->onSave();
    ui->SubstituteDecimal->onSave();
    ui->UseLocaleFormatting->onSave();
    ui->EnableCursorBlinking->onSave();
    ui->SplashScreen->onSave();
    ui->CmdHistorySize->onSave();
    ui->checkPopUpWindow->onSave();
    ui->toolbarIconSize->onSave();
    ui->workbenchTabIconSize->onSave();
    ui->checkboxTaskList->onSave();
    ui->toolTipIconSize->onSave();
    saveTreeMode(ui->treeMode->currentIndex());
}

void DlgSettingsGeneral::setupToolBarIconSize()
{
    QComboBox *comboBox = ui->toolbarIconSize;
    int current = getMainWindow()->iconSize().width();
    QSignalBlocker blocker(comboBox);
    comboBox->clear();
    comboBox->addItem(tr("Small (%1px)").arg(16), QVariant((int)16));
    comboBox->addItem(tr("Medium (%1px)").arg(24), QVariant((int)24));
    comboBox->addItem(tr("Large (%1px)").arg(32), QVariant((int)32));
    comboBox->addItem(tr("Extra large (%1px)").arg(48), QVariant((int)48));
    int idx = comboBox->findData(QVariant(current));
    if (idx < 0) {
        comboBox->addItem(tr("Custom (%1px)").arg(current), QVariant((int)current));
        idx = 4;
    }
    comboBox->setCurrentIndex(idx);
}

void DlgSettingsGeneral::loadSettings()
{
    int FracInch;
    int cbIndex;

    ParameterGrp::handle hGrpu = App::GetApplication().GetParameterGroupByPath
    ("User parameter:BaseApp/Preferences/Units");
    ui->comboBox_UnitSystem->setCurrentIndex(hGrpu->GetInt("UserSchema", 0));
    ui->spinBoxDecimals->setValue(hGrpu->GetInt("Decimals", Base::UnitsApi::getDecimals()));
    ui->checkBox_projectUnitSystemIgnore->setChecked(hGrpu->GetBool("IgnoreProjectSchema", false));

    // Get the current user setting for the minimum fractional inch
    FracInch = hGrpu->GetInt("FracInch", Base::QuantityFormat::getDefaultDenominator());

    // Convert fractional inch to the corresponding combobox index using this
    // handy little equation.
    cbIndex = std::log2(FracInch) - 1;
    ui->comboBox_FracInch->setCurrentIndex(cbIndex);

    ParameterGrp::handle hGrp = WindowParameter::getDefaultParameter()->GetGroup("General");

    // search for the language files
    QString langToStr = QLocale::languageToString(QLocale().language());
    QByteArray language = langToStr.toUtf8();

    int index = 1;
    TStringMap list = Translator::instance()->supportedLocales();
    ui->Languages->clear();
    ui->Languages->addItem(QStringLiteral("English"), QByteArray("English"));
    for (TStringMap::iterator it = list.begin(); it != list.end(); ++it, index++) {
        QByteArray lang = it->first.c_str();
        QString langname = QString::fromUtf8(lang.constData());

        if (it->second == "sr-CS") {
            // Qt does not treat sr-CS (Serbian, Latin) as a Latin-script variant by default: this
            // forces it to do so.
            it->second = "sr_Latn";
        }

        QLocale locale(QString::fromUtf8(it->second.c_str()));
        QString native = locale.nativeLanguageName();
        if (!native.isEmpty()) {
            if (native[0].isLetter())
                native[0] = native[0].toUpper();
            langname = native;
        }

        ui->Languages->addItem(langname, lang);
        if (language == lang) {
            ui->Languages->setCurrentIndex(index);
        }
    }

    QAbstractItemModel* model = ui->Languages->model();
    if (model)
        model->sort(0);

    setupToolBarIconSize();
    ui->toolbarIconSize->onRestore();

    ui->treeMode->addItem(tr("Combo View"));
    ui->treeMode->addItem(tr("TreeView and PropertyView"));
    ui->treeMode->addItem(tr("Both"));

    hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/DockWindows");
    bool propertyView = hGrp->GetGroup("PropertyView")->GetBool("Enabled",false);
    bool treeView = hGrp->GetGroup("TreeView")->GetBool("Enabled",false);
    bool comboView = hGrp->GetGroup("ComboView")->GetBool("Enabled",true);
    index = 0;
    if(propertyView || treeView) {
        index = comboView?2:1;
    }
    ui->treeMode->setCurrentIndex(index);
    QObject::connect(ui->treeMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
        [this](int value) {
            if (PrefParam::AutoSave()) saveTreeMode(value);
        });

    ui->toolbarArea->addItem(tr("Top"), QByteArray("Top"));
    ui->toolbarArea->addItem(tr("Left"), QByteArray("Left"));
    ui->toolbarArea->addItem(tr("Right"), QByteArray("Right"));
    ui->toolbarArea->addItem(tr("Bottom"), QByteArray("Bottom"));

    ui->globalToolbarArea->addItem(tr("Top"), QByteArray("Top"));
    ui->globalToolbarArea->addItem(tr("Left"), QByteArray("Left"));
    ui->globalToolbarArea->addItem(tr("Right"), QByteArray("Right"));
    ui->globalToolbarArea->addItem(tr("Bottom"), QByteArray("Bottom"));

    ui->AutoApply->onRestore();
    QObject::connect(ui->AutoApply, &PrefCheckBox::toggled, [this](bool checked) {
        // Always auto apply for AutoApply option itself.
        PrefParam::setAutoSave(checked);
    });

    ui->SaveParameter->onRestore();
    ui->Languages->onRestore();
    ui->toolbarArea->onRestore();
    ui->globalToolbarArea->onRestore();
    ui->treeIconSize->onRestore();
    ui->treeFontSize->onRestore();;

    ui->treeItemSpacing->setValue(TreeParams::defaultItemSpacing());
    ui->treeItemSpacing->onRestore();

    ui->checkboxTaskList->onRestore();

    ui->appFontSize->onRestore();
    ui->SubstituteDecimal->onRestore();
    ui->UseLocaleFormatting->onRestore();
    ui->EnableCursorBlinking->onRestore();
    ui->RecentFiles->onRestore();
    ui->SplashScreen->onRestore();

    ui->CmdHistorySize->setValue(ViewParams::defaultCommandHistorySize());
    ui->CmdHistorySize->onRestore();

    ui->checkPopUpWindow->setChecked(ViewParams::defaultCheckWidgetPlacementOnRestore());
    ui->checkPopUpWindow->onRestore();

    ui->workbenchTabIconSize->onRestore();

    ui->toolTipIconSize->onRestore();

    updateLanguage();
}

void DlgSettingsGeneral::updateLanguage()
{
    int index = ui->comboBox_UnitSystem->currentIndex();
    ui->retranslateUi(this);
    ui->comboBox_UnitSystem->setCurrentIndex(index);
    ui->toolTipIconSize->setToolTip(QApplication::translate("ViewParams",
                ViewParams::docToolTipIconSize()));
    setupToolBarIconSize();
}

void DlgSettingsGeneral::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        updateLanguage();
    }
    else {
        QWidget::changeEvent(event);
    }
}

void DlgSettingsGeneral::onUnitSystemIndexChanged(int index)
{
    if (index < 0)
        return; // happens when clearing the combo box in retranslateUi()

    // Enable/disable the fractional inch option depending on system
    if (static_cast<UnitSystem>(index) == UnitSystem::ImperialBuilding)
    {
        ui->comboBox_FracInch->setVisible(true);
        ui->fractionalInchLabel->setVisible(true);
    }
    else
    {
        ui->comboBox_FracInch->setVisible(false);
        ui->fractionalInchLabel->setVisible(false);
    }
}

///////////////////////////////////////////////////////////
namespace {

void applyToolTipIconSize(ParameterGrp *)
{
    Application::Instance->commandManager().refreshIcons();
}

void applyToolbarIconSize(ParameterGrp *)
{
    ToolBarManager::getInstance()->setupToolBarIconSize();
    if (_Instance)
        _Instance->setupToolBarIconSize();
}

void applyLanguage(const ParamKey *paramKey)
{
    QString lang = QLocale::languageToString(QLocale().language());
    std::string language = paramKey->hGrp->GetASCII(paramKey->key, (const char*)lang.toUtf8());
    Translator::instance()->activateLanguage(language.c_str());
}

void applyNumberLocale(ParameterGrp *hGrp)
{
    int localeFormat = hGrp->GetInt("UseLocaleFormatting", 0);
    if (localeFormat == 0) {
        Translator::instance()->setLocale(); // Defaults to system locale
    }
    else if (localeFormat == 1) {
        QString lang = QLocale::languageToString(QLocale().language());
        std::string language = hGrp->GetASCII("Language", (const char*)lang.toUtf8());
        Translator::instance()->setLocale(language.c_str());
    }
    else if (localeFormat == 2) {
        Translator::instance()->setLocale("C");
    }
}

void applyCursorBlinking(const ParamKey *paramKey)
{
    int blinkTime = paramKey->hGrp->GetBool(paramKey->key, true) ? -1 : 0;
    qApp->setCursorFlashTime(blinkTime);
}

void applyDecimalPointConversion(const ParamKey *paramKey)
{
    Translator::instance()->enableDecimalPointConversion(paramKey->hGrp->GetBool(paramKey->key, false));
}

void applyPythonWordWrap(const ParamKey *paramKey)
{
    QWidget* pc = DockWindowManager::instance()->getDockWindow("Python console");
    PythonConsole *pcPython = qobject_cast<PythonConsole*>(pc);
    if (pcPython) {
        bool pythonWordWrap = paramKey->hGrp->GetBool(paramKey->key, true);

        if (pythonWordWrap) {
            pcPython->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        } else {
            pcPython->setWordWrapMode(QTextOption::NoWrap);
        }
    }
}

class ApplyDockWidget: public ParamHandler {
public:
    bool onChange(const ParamKey *) override
    {
        OverlayManager::instance()->reload(OverlayManager::ReloadMode::ReloadPause);
        return true;
    }

    void onTimer() override
    {
        getMainWindow()->initDockWindows(true);
        OverlayManager::instance()->reload(OverlayManager::ReloadMode::ReloadResume);
    }
};

} // anonymous namespace

void DlgSettingsGeneral::attachObserver()
{
    static ParamHandlers handlers;

    // Appearance is owned by DlgSettingsTheme, which registers the handler for
    // it. Two pages once registered one each on overlapping keys, so every
    // stylesheet change ran the apply twice.

    auto hDockWindows = App::GetApplication().GetUserParameter().GetGroup("BaseApp/Preferences/DockWindows");
    auto applyDockWidget = std::shared_ptr<ParamHandler>(new ApplyDockWidget);
    handlers.addHandler(ParamKey(hDockWindows->GetGroup("ComboView"), "Enabled"), applyDockWidget);
    handlers.addHandler(ParamKey(hDockWindows->GetGroup("TreeView"), "Enabled"), applyDockWidget);
    handlers.addHandler(ParamKey(hDockWindows->GetGroup("PropertyView"), "Enabled"), applyDockWidget);
    handlers.addHandler(ParamKey(hDockWindows->GetGroup("DAGView"), "Enabled"), applyDockWidget);
    handlers.addHandler(ParamKey(hDockWindows->GetGroup("TaskWatcher"), "Enabled"), applyDockWidget);

    handlers.addDelayedHandler("BaseApp/Preferences/View", "ToolTipIconSize", applyToolTipIconSize);

    auto hGeneral = App::GetApplication().GetUserParameter().GetGroup("BaseApp/Preferences/General");
    handlers.addDelayedHandler(hGeneral, {"Language", "UseLocaleFormatting"}, applyNumberLocale);
    handlers.addHandler(hGeneral, "Language", applyLanguage);
    handlers.addHandler(hGeneral, "SubstituteDecimal", applyDecimalPointConversion);
    handlers.addHandler(hGeneral, "EnableCursorBlinking", applyCursorBlinking);
    handlers.addDelayedHandler(hGeneral,
                              {"ToolbarIconSize",
                               "StatusBarIconSize",
                               "MenuBarIconSize",
                               "WorkbenchTabIconSize",
                               "WorkbenchComboIconSize",},
                              applyToolbarIconSize);
    handlers.addHandler(hGeneral, "PythonWordWrap", applyPythonWordWrap);

    ViewParams::init();
}

#include "moc_DlgSettingsGeneral.cpp"
