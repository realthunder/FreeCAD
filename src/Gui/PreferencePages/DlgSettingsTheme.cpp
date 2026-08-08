/***************************************************************************
 *   Copyright (c) 2009 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <algorithm>
# include <limits>
# include <QDir>
# include <QDoubleSpinBox>
# include <QFileInfo>
# include <QFormLayout>
# include <QInputDialog>
# include <QLineEdit>
# include <QMessageBox>
# include <QPushButton>
# include <QRegularExpression>
# include <QSpinBox>
#endif

#include <App/Color.h>

#include <Gui/Application.h>
#include <Gui/Widgets.h>
#include <Gui/DlgPreferencesImp.h>
#include <Gui/ParamHandler.h>
#include <Gui/PreferencePackManager.h>
#include <Gui/PrefWidgets.h>
#include <Gui/ThemeManager.h>

#include "DlgSettingsTheme.h"
#include "ui_DlgSettingsTheme.h"


using namespace Gui;
using namespace Gui::Dialog;

static DlgSettingsTheme* _Instance;

namespace {
/// The combo entry standing for "the appearance matches no theme".
const char* CustomThemeMarker = "\x01custom";
}

/* TRANSLATOR Gui::Dialog::DlgSettingsTheme */

DlgSettingsTheme::DlgSettingsTheme(QWidget* parent)
    : PreferencePage(parent)
    , ui(new Ui_DlgSettingsTheme)
{
    ui->setupUi(this);

    connect(ui->themesCombobox, qOverload<int>(&QComboBox::activated),
            this, &DlgSettingsTheme::onThemeActivated);
    connect(ui->revertButton, &QPushButton::clicked,
            this, &DlgSettingsTheme::onRevertClicked);
    connect(ui->saveAsThemeButton, &QPushButton::clicked,
            this, &DlgSettingsTheme::onSaveAsThemeClicked);

    _Instance = this;
}

DlgSettingsTheme::~DlgSettingsTheme()
{
    // The dialog can build a replacement page before tearing the old one down,
    // in which case the replacement already owns the slot.
    if (_Instance == this) {
        _Instance = nullptr;
    }
}

void DlgSettingsTheme::saveSettings()
{
    ui->ColorScheme->onSave();
    ui->StyleSheets->onSave();
    ui->OverlayStyleSheets->onSave();
    ui->MenuStyleSheets->onSave();
    ui->IconSets->onSave();
    ui->IconSetPolicy->onSave();
    ui->ThemeAccentColor1->onSave();
    ui->ThemeAccentColor2->onSave();
    ui->ThemeAccentColor3->onSave();
    ui->tiledBackground->onSave();
    saveVariables();

    refreshModifiedState();
}

void DlgSettingsTheme::loadSettings()
{
    loadThemes();
    loadCustomization();
    refreshModifiedState();
}

void DlgSettingsTheme::loadThemes()
{
    const QSignalBlocker blocker(ui->themesCombobox);
    ui->themesCombobox->clear();
    ui->themesCombobox->addItem(tr("(Custom)"), QString::fromUtf8(CustomThemeMarker));

    Application::Instance->prefPackManager()->rescan();
    for (const auto& pack : Application::Instance->prefPackManager()->preferencePacks()) {
        if (pack.second.metadata().type() == "Theme") {
            ui->themesCombobox->addItem(QString::fromStdString(pack.first),
                                        QString::fromStdString(pack.first));
        }
    }

    const auto current = QString::fromStdString(ThemeManager::currentTheme());
    const int index = current.isEmpty() ? 0 : ui->themesCombobox->findData(current);
    ui->themesCombobox->setCurrentIndex(index >= 0 ? index : 0);
}

void DlgSettingsTheme::loadCustomization()
{
    // These two are short fixed lists rather than a directory scan, so they are
    // filled here where their labels can be translated.
    // The data has to be a QByteArray: PrefComboBox reads prefType from the .ui,
    // which is a cstring, and then looks the stored value up with
    // findData(QByteArray). A QString would never match, leaving the combo on
    // whatever it happened to show and saving that back over the parameter.
    const QSignalBlocker schemeBlocker(ui->ColorScheme);
    ui->ColorScheme->clear();
    ui->ColorScheme->addItem(tr("Match desktop"), QByteArray(""));
    ui->ColorScheme->addItem(tr("Light"), QByteArray("Light"));
    ui->ColorScheme->addItem(tr("Dark"), QByteArray("Dark"));
    ui->ColorScheme->onRestore();

    const QSignalBlocker policyBlocker(ui->IconSetPolicy);
    ui->IconSetPolicy->clear();
    ui->IconSetPolicy->addItem(tr("Use the theme's icon set"), QByteArray("Reset"));
    ui->IconSetPolicy->addItem(tr("Layer the theme's over mine"), QByteArray("Merge"));
    ui->IconSetPolicy->addItem(tr("Keep my icon set"), QByteArray("Keep"));
    ui->IconSetPolicy->onRestore();

    populateStylesheets("StyleSheet", "qss", ui->StyleSheets, "No style sheet");
    populateStylesheets("OverlayActiveStyleSheet", "overlay", ui->OverlayStyleSheets, "Auto");
    populateStylesheets("MenuStyleSheet", "qssm", ui->MenuStyleSheets, "Auto");
    populateStylesheets("IconSet", "iconset", ui->IconSets, "None",
                        QStringList(QStringLiteral("*.txt")));

    ui->ThemeAccentColor1->onRestore();
    ui->ThemeAccentColor2->onRestore();
    ui->ThemeAccentColor3->onRestore();
    ui->tiledBackground->onRestore();

    loadVariables();
}

namespace
{
ParameterGrp::handle themeVariables()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Themes/Variables");
}
}  // namespace

void DlgSettingsTheme::loadVariables()
{
    // A theme names its own variables, so there is no fixed set of widgets to
    // put in the .ui: build a row per variable, typed by how it is stored.
    variableRows.clear();
    while (ui->variablesLayout->rowCount() > 0) {
        ui->variablesLayout->removeRow(0);
    }

    auto hVariables = themeVariables();
    auto entries = hVariables->GetParameterNames();
    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second < rhs.second;
    });

    for (const auto& entry : entries) {
        const auto& name = entry.second;
        QWidget* editor = nullptr;

        switch (entry.first) {
            case ParameterGrp::ParamType::FCUInt: {
                auto* button = new ColorButton(this);
                button->setColor(App::Color::fromPackedRGBA<QColor>(
                    static_cast<unsigned int>(hVariables->GetUnsigned(name.c_str(), 0))));
                editor = button;
                break;
            }
            case ParameterGrp::ParamType::FCText: {
                auto* edit = new QLineEdit(this);
                edit->setText(QString::fromStdString(hVariables->GetASCII(name.c_str())));
                editor = edit;
                break;
            }
            case ParameterGrp::ParamType::FCInt: {
                auto* spin = new QSpinBox(this);
                spin->setRange(std::numeric_limits<int>::min(),
                               std::numeric_limits<int>::max());
                spin->setValue(static_cast<int>(hVariables->GetInt(name.c_str(), 0)));
                editor = spin;
                break;
            }
            case ParameterGrp::ParamType::FCFloat: {
                auto* spin = new QDoubleSpinBox(this);
                spin->setDecimals(3);
                spin->setRange(-1e6, 1e6);
                spin->setValue(hVariables->GetFloat(name.c_str(), 0.0));
                editor = spin;
                break;
            }
            default:
                // A stylesheet has no use for the remaining types, and showing
                // an editor that cannot be substituted would only mislead.
                continue;
        }

        ui->variablesLayout->addRow(QString::fromStdString(name), editor);
        variableRows.push_back({name, entry.first, editor});
    }

    ui->variablesGroup->setVisible(!variableRows.empty());
}

void DlgSettingsTheme::saveVariables()
{
    auto hVariables = themeVariables();
    for (const auto& row : variableRows) {
        switch (row.type) {
            case ParameterGrp::ParamType::FCUInt:
                hVariables->SetUnsigned(row.name.c_str(),
                                        App::Color::asPackedRGBA<QColor>(
                                            static_cast<ColorButton*>(row.editor)->color()));
                break;
            case ParameterGrp::ParamType::FCText:
                hVariables->SetASCII(
                    row.name.c_str(),
                    static_cast<QLineEdit*>(row.editor)->text().toUtf8().constData());
                break;
            case ParameterGrp::ParamType::FCInt:
                hVariables->SetInt(row.name.c_str(),
                                   static_cast<QSpinBox*>(row.editor)->value());
                break;
            case ParameterGrp::ParamType::FCFloat:
                hVariables->SetFloat(row.name.c_str(),
                                     static_cast<QDoubleSpinBox*>(row.editor)->value());
                break;
            default:
                break;
        }
    }
}

void DlgSettingsTheme::refreshFromParameters()
{
    // Deliberately no rescan(): the packs on disk have not changed, only the
    // parameters, and this runs on every appearance change.
    const auto current = QString::fromStdString(ThemeManager::currentTheme());
    const int index = current.isEmpty() ? 0 : ui->themesCombobox->findData(current);
    {
        const QSignalBlocker blocker(ui->themesCombobox);
        ui->themesCombobox->setCurrentIndex(index >= 0 ? index : 0);
    }

    loadCustomization();
    refreshModifiedState();
}

void DlgSettingsTheme::refreshModifiedState()
{
    const bool named = !ThemeManager::currentTheme().empty();
    const bool customised = ThemeManager::isCustomised();

    ui->modifiedLabel->setVisible(named && customised);
    ui->revertButton->setEnabled(named && customised);
}

void DlgSettingsTheme::applyTheme(const QString& name)
{
    if (name.isEmpty()) {
        return;
    }

    try {
        Application::Instance->prefPackManager()->apply(name.toStdString());
    }
    catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Theme"), QString::fromUtf8(e.what()));
        return;
    }

    // The pack rewrote far more than this page shows, so let every page reread
    // its parameters. reload() calls loadSettings() on each, including this one.
    if (auto parentDialog = qobject_cast<DlgPreferencesImp*>(this->window())) {
        parentDialog->reload();
    }
    else {
        loadSettings();
    }
}

void DlgSettingsTheme::onThemeActivated(int index)
{
    const QString name = ui->themesCombobox->itemData(index).toString();
    if (name == QString::fromUtf8(CustomThemeMarker)) {
        // "(Custom)" describes a state, it does not produce one. Put the combo
        // back on whatever is actually in effect.
        loadThemes();
        return;
    }
    applyTheme(name);
}

void DlgSettingsTheme::onRevertClicked()
{
    applyTheme(QString::fromStdString(ThemeManager::currentTheme()));
}

void DlgSettingsTheme::onSaveAsThemeClicked()
{
    auto* manager = Application::Instance->prefPackManager();

    // Everything this page edits is what Theme.cfg lists, so that one template
    // is the whole of a theme.
    const auto templates = manager->templateFiles();
    std::vector<PreferencePackManager::TemplateFile> themeTemplate;
    for (const auto& file : templates) {
        if (file.name == "Theme") {
            themeTemplate.push_back(file);
            break;
        }
    }
    if (themeTemplate.empty()) {
        QMessageBox::warning(this, tr("Save as theme"),
                             tr("The Theme preference pack template is missing from this "
                                "installation, so the current appearance cannot be saved."));
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save as theme"), tr("Theme name:"),
                                               QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    if (name.contains(QRegularExpression(QStringLiteral(R"([/\\?%*:|"<>])")))) {
        QMessageBox::warning(this, tr("Save as theme"),
                             tr("A theme name cannot contain any of / \\ ? % * : | \" < >"));
        return;
    }

    const auto existing = manager->preferencePackNames();
    if (std::find(existing.begin(), existing.end(), name.toStdString()) != existing.end()) {
        const auto answer = QMessageBox::warning(this, tr("Save as theme"),
                                                 tr("A preference pack named \"%1\" exists "
                                                    "already. Overwrite it?").arg(name),
                                                 QMessageBox::Yes | QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    // Anything shown on this page that is still only in a widget would not be
    // saved, since the pack is built from the parameters.
    saveSettings();

    manager->save(name.toStdString(), themeTemplate, "Theme");
    manager->rescan();
    ThemeManager::setCurrentTheme(name.toStdString());

    loadThemes();
    refreshModifiedState();
}

void DlgSettingsTheme::populateStylesheets(const char *key,
                                           const char *path,
                                           PrefComboBox *combo,
                                           const char *def,
                                           QStringList filter)
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow");
    // List all .qss/.css files
    QMap<QString, QString> cssFiles;
    QDir dir;
    if (filter.isEmpty()) {
        filter << QStringLiteral("*.qss");
        filter << QStringLiteral("*.css");
    }
    QFileInfoList fileNames;

    // read from user, resource and built-in directory
    QStringList qssPaths = QDir::searchPaths(QString::fromUtf8(path));
    for (QStringList::iterator it = qssPaths.begin(); it != qssPaths.end(); ++it) {
        dir.setPath(*it);
        fileNames = dir.entryInfoList(filter, QDir::Files, QDir::Name);
        for (QFileInfoList::iterator jt = fileNames.begin(); jt != fileNames.end(); ++jt) {
            if (cssFiles.find(jt->baseName()) == cssFiles.end()) {
                cssFiles[jt->baseName()] = jt->fileName();
            }
        }
    }

    const QSignalBlocker blocker(combo);
    combo->clear();

    // now add all unique items. The data is a QByteArray to match the cstring
    // prefType the .ui declares, which is what PrefComboBox looks values up by.
    combo->addItem(tr(def), QByteArray());
    for (QMap<QString, QString>::iterator it = cssFiles.begin(); it != cssFiles.end(); ++it) {
        combo->addItem(it.key(), it.value().toUtf8());
    }

    QString selectedStyleSheet = QString::fromUtf8(hGrp->GetASCII(key).c_str());
    int index = combo->findData(selectedStyleSheet.toUtf8());

    // might be an absolute path name
    if (index < 0 && !selectedStyleSheet.isEmpty()) {
        QFileInfo fi(selectedStyleSheet);
        if (fi.isAbsolute()) {
            QString path = fi.absolutePath();
            if (qssPaths.indexOf(path) >= 0) {
                selectedStyleSheet = fi.fileName();
            }
            else {
                selectedStyleSheet = fi.absoluteFilePath();
            }
        }
        index = combo->findData(selectedStyleSheet.toUtf8());
    }

    // A value we cannot show is still a value the user or a theme chose. Saving
    // an unrepresentable selection back would write an empty string over it,
    // which is how a pack's overlay sheet used to disappear on merely opening
    // this page.
    if (index < 0 && !selectedStyleSheet.isEmpty()) {
        combo->addItem(QFileInfo(selectedStyleSheet).baseName(), selectedStyleSheet.toUtf8());
        index = combo->count() - 1;
    }

    // onRestore() falls back to whatever the combo held when it was first
    // restored whenever the parameter is absent -- and applying a theme removes
    // the keys it does not name, so the widget would put the outgoing theme's
    // value straight back and saveSettings() would write it to the config. The
    // parameter is the authority: let the widget do its bookkeeping, then say
    // what is selected.
    combo->onRestore();
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void DlgSettingsTheme::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        loadCustomization();
    }
    else {
        QWidget::changeEvent(e);
    }
}

namespace {

void applyStyleSheet(ParameterGrp *hGrp)
{
    // Settle the palette first: a theme that ships no stylesheet draws straight
    // from it, and setStyleSheet() samples the palette for the link color.
    Application::applyColorScheme();
    auto sheet = hGrp->GetASCII("StyleSheet");
    bool tiledBG = hGrp->GetBool("TiledBackground", false);
    Application::Instance->setStyleSheet(QString::fromUtf8(sheet.c_str()), tiledBG);

    // Whoever wrote the parameter -- this page, the Start wizard, a macro --
    // may have just made the appearance stop matching the recorded theme.
    if (_Instance) {
        _Instance->refreshFromParameters();
    }
}

} // anonymous namespace

void DlgSettingsTheme::attachObserver()
{
    static ParamHandlers handlers;

    auto handler = handlers.addDelayedHandler("BaseApp/Preferences/MainWindow",
                                              {"StyleSheet",
                                               "TiledBackground",
                                               "IconSet",
                                               "ColorScheme",
                                               "MenuStyleSheet",
                                               "OverlayActiveStyleSheet",
                                               "Theme"},
                                              applyStyleSheet);
    handlers.addHandler("BaseApp/Preferences/Themes",
                        {"ThemeAccentColor1", "ThemeAccentColor2", "ThemeAccentColor3"},
                        handler);

    // A theme names its own variables, so there is no key list to register:
    // the empty key takes the whole group, and editing any of them repaints
    // through the same apply as everything else here.
    handlers.addHandler("BaseApp/Preferences/Themes/Variables", "", handler);
}

#include "moc_DlgSettingsTheme.cpp"
