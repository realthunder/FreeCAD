/***************************************************************************
 *   Copyright (c) 2007 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QButtonGroup>
# include <QRegularExpression>
# include <QRegularExpressionValidator>
# include <QVBoxLayout>
# include <Interface_Static.hxx>
#endif

#include <boost/algorithm/string/predicate.hpp>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Mod/Part/App/Interface.h>
#include <Mod/Part/App/IGES/ImportExportSettings.h>
#include <Mod/Part/App/OCAF/ImportExportSettings.h>
#include <Mod/Part/App/STEP/ImportExportSettings.h>

#include "DlgSettingsGeneral.h"
#include "ui_DlgSettingsGeneral.h"
#include "ui_DlgImportExportIges.h"
#include "DlgExportStep.h"
#include "DlgImportStep.h"


using namespace PartGui;

DlgSettingsGeneral::DlgSettingsGeneral(QWidget* parent)
  : PreferencePage(parent), ui(new Ui_DlgSettingsGeneral)
{
    ui->setupUi(this);

    QObject::connect(ui->btnAuxGroup, &QPushButton::pressed, [this]() {
        bool checked = ui->checkBoxAuxGroup->isChecked();
        const char *typeName = "PartDesign::AuxGroup";
        try {
            Base::Type::importModule(typeName);
            auto type = Base::Type::fromName(typeName);

            std::vector<std::pair<App::DocumentObjectT, const char*>> objs;

            auto checkName = [&objs](App::DocumentObject *obj, const char *name) {
                if (boost::starts_with(obj->getNameInDocument(), name)) {
                    objs.emplace_back(obj, name);
                    return true;
                }
                return false;
            };

            for (auto doc : App::GetApplication().getDocuments()) {
                if (doc->testStatus(App::Document::PartialDoc))
                    continue;
                for (auto obj : doc->getObjects()) {
                    if (!obj->isDerivedFrom(type)) continue;
                    if (!checkName(obj, "Sketches") && !checkName(obj, "Datums"))
                        checkName(obj, "Misc");
                }
            }

            if (checked) {
                // About to change back to unique indexed label (i.e. the
                // group's internal name). There may be a small chance of label
                // conflict here where a group uses an indexed label that
                // happens to be another group's internal name. So we first
                // change all groups to some temporary name, and later reset
                // their label to their internal name together.
                std::string tmp;
                for (auto &v : objs) {
                    if (auto obj = v.first.getObject()) {
                        tmp = std::string(v.second) + "_";
                        obj->Label.setValue(tmp);
                    }
                }
                for (auto &v : objs) {
                    if (auto obj = v.first.getObject())
                        obj->Label.setValue(obj->getNameInDocument());
                }
            } else {
                for (auto &v : objs) {
                    if (auto obj = v.first.getObject())
                        obj->Label.setValue(v.second);
                }
            }
        } catch (Base::Exception &e) {
            e.ReportException();
            return;
        }
    });
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgSettingsGeneral::~DlgSettingsGeneral() = default;

void DlgSettingsGeneral::saveSettings()
{
    ui->checkBooleanCheck->onSave();
    ui->checkBooleanRefine->onSave();
    ui->checkSketchBaseRefine->onSave();
    ui->checkLinearize->onSave();
    ui->checkSingleSolid->onSave();
    ui->checkObjectNaming->onSave();
    ui->comboBoxCommandOverride->onSave();
    ui->comboBoxWrapFeature->onSave();
    ui->checkAutoGroupSolids->onSave();
    ui->checkBoxAuxGroup->onSave();
    ui->checkSplitEllipsoid->onSave();
    ui->checkBoxValidateShape->onSave();
    ui->checkBoxFixShape->onSave();
}

void DlgSettingsGeneral::loadSettings()
{
    ui->checkBooleanCheck->onRestore();
    ui->checkBooleanRefine->onRestore();
    ui->checkSketchBaseRefine->onRestore();
    ui->checkLinearize->onRestore();
    ui->checkSingleSolid->onRestore();
    ui->checkObjectNaming->onRestore();
    ui->comboBoxCommandOverride->onRestore();
    ui->comboBoxWrapFeature->onRestore();
    ui->checkAutoGroupSolids->onRestore();
    ui->checkBoxAuxGroup->onRestore();
    ui->checkSplitEllipsoid->onRestore();
    ui->checkBoxValidateShape->onRestore();
    ui->checkBoxFixShape->onRestore();
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void DlgSettingsGeneral::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

// ----------------------------------------------------------------------------

DlgImportExportIges::DlgImportExportIges(QWidget* parent)
  : PreferencePage(parent), ui(new Ui_DlgImportExportIges)
{
    ui->setupUi(this);
    ui->lineEditProduct->setReadOnly(true);

    bg = new QButtonGroup(this);
    bg->addButton(ui->radioButtonBRepOff, 0);
    bg->addButton(ui->radioButtonBRepOn, 1);

    QRegularExpression rx;
    rx.setPattern(QStringLiteral("[\\x00-\\x7F]+"));
    QRegularExpressionValidator* companyValidator = new QRegularExpressionValidator(ui->lineEditCompany);
    companyValidator->setRegularExpression(rx);
    ui->lineEditCompany->setValidator(companyValidator);
    QRegularExpressionValidator* authorValidator = new QRegularExpressionValidator(ui->lineEditAuthor);
    authorValidator->setRegularExpression(rx);
    ui->lineEditAuthor->setValidator(authorValidator);
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgImportExportIges::~DlgImportExportIges() = default;

void DlgImportExportIges::saveSettings()
{
    Part::IGES::ImportExportSettings settings;

    int unit = ui->comboBoxUnits->currentIndex();
    settings.setUnit(static_cast<Part::Interface::Unit>(unit));
    settings.setBRepMode(bg->checkedId() == 1);

    // Import
    settings.setSkipBlankEntities(ui->checkSkipBlank->isChecked());

    // header info
    settings.setCompany(ui->lineEditCompany->text().toUtf8());
    settings.setAuthor(ui->lineEditAuthor->text().toUtf8());
}

void DlgImportExportIges::loadSettings()
{
    Part::IGES::ImportExportSettings settings;

    ui->comboBoxUnits->setCurrentIndex(static_cast<int>(settings.getUnit()));

    bool brep = settings.getBRepMode();
    if (brep)
        ui->radioButtonBRepOn->setChecked(true);
    else
        ui->radioButtonBRepOff->setChecked(true);

    // Import
    ui->checkSkipBlank->setChecked(settings.getSkipBlankEntities());

    // header info
    ui->lineEditCompany->setText(QString::fromStdString(settings.getCompany()));
    ui->lineEditAuthor->setText(QString::fromStdString(settings.getAuthor()));
    ui->lineEditProduct->setText(QString::fromStdString(settings.getProductName()));
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void DlgImportExportIges::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

// ----------------------------------------------------------------------------

DlgImportExportStep::DlgImportExportStep(QWidget* parent)
  : PreferencePage(parent)
  , exportStep(new DlgExportStep(this))
  , importStep(new DlgImportStep(this))
  , headerStep(new DlgExportHeaderStep(this))
{
    setWindowTitle(tr("STEP"));
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    layout->addWidget(exportStep);
    layout->addWidget(importStep);
    layout->addWidget(headerStep);

    QSpacerItem* verticalSpacer = new QSpacerItem(20, 82, QSizePolicy::Minimum, QSizePolicy::Expanding);
    layout->addItem(verticalSpacer);
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgImportExportStep::~DlgImportExportStep() = default;

void DlgImportExportStep::saveSettings()
{
    exportStep->saveSettings();
    importStep->saveSettings();
    headerStep->saveSettings();
}

void DlgImportExportStep::loadSettings()
{
    exportStep->loadSettings();
    importStep->loadSettings();
    headerStep->loadSettings();
}

void DlgImportExportStep::changeEvent(QEvent *)
{
    // do nothing
}


#include "moc_DlgSettingsGeneral.cpp"
