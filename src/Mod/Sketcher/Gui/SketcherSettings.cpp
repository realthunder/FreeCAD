/***************************************************************************
 *   Copyright (c) 2014 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include <Mod/Sketcher/App/SketcherParams.h>

#include <Gui/ViewParams.h>
#ifndef _PreComp_
#include <QApplication>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <array>
#endif

#include <App/Application.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/Workbench.h>
#include <Gui/WorkbenchManager.h>

#include "SketcherSettings.h"
#include "ui_SketcherSettings.h"
#include "ui_SketcherSettingsAppearance.h"
#include "ui_SketcherSettingsDisplay.h"
#include "ui_SketcherSettingsGrid.h"


using namespace SketcherGui;

namespace
{
// A line pattern as the grid is drawn with it: sixteen bits, a set bit a
// drawn pixel (upstream's PenStyle)
struct PenStyle
{
    uint16_t pattern;

    QVector<qreal> toDashPattern() const
    {
        QVector<qreal> dashPattern;
        int count = 0;
        bool isDash = (pattern & 0x8000) != 0;  // Check the highest bit

        for (int i = 0; i < 16; ++i) {
            bool currentBit = (pattern & (0x8000 >> i)) != 0;
            if (currentBit == isDash) {
                ++count;  // Counting dashes or spaces
            }
            else {
                // Adjust count to be odd for dashes and even for spaces (see qt doc)
                count = (count % 2 == (isDash ? 0 : 1)) ? count + 1 : count;
                dashPattern << count;
                count = 1;  // Reset count for next dash/space
                isDash = !isDash;
            }
        }
        count = (count % 2 == (isDash ? 0 : 1)) ? count + 1 : count;
        dashPattern << count;  // Add the last count

        if ((dashPattern.size() % 2) == 1) {
            // prevent this error : qWarning("QPen::setDashPattern: Pattern not of even length");
            dashPattern << 1;
        }

        return dashPattern;
    }

    QIcon toIcon(QSize size, qreal dpr, const QBrush& brush) const
    {
        QPixmap px(size * dpr);
        px.setDevicePixelRatio(dpr);
        px.fill(Qt::transparent);

        QPen pen;
        pen.setDashPattern(toDashPattern());
        pen.setBrush(brush);
        pen.setWidth(2);

        QPainter painter(&px);
        painter.setPen(pen);
        auto mid = size.height() / 2;
        painter.drawLine(0, mid, size.width(), mid);
        painter.end();

        return px;
    }
};

constexpr auto PenStyles = std::to_array<PenStyle>({
    {.pattern = 0b1111111111111111},  // solid
    {.pattern = 0b1110111011101110},  // dashed 3:1
    {.pattern = 0b1111110011111100},  // dashed 6:2
    {.pattern = 0b0000111100001111},  // dashed 4:4
    {.pattern = 0b1010101010101010},  // point 1:1
    {.pattern = 0b1110010011100100},  // dash point
    {.pattern = 0b1111111100111100},  // dash long-dash
});

constexpr QSize LineIconSize(80, 12);
}  // namespace

/* TRANSLATOR SketcherGui::SketcherSettings */

SketcherSettings::SketcherSettings(QWidget* parent)
    : PreferencePage(parent)
    , ui(new Ui_SketcherSettings)
{
    ui->setupUi(this);
}

/**
 *  Destroys the object and frees any allocated resources
 */
SketcherSettings::~SketcherSettings()
{
    // no need to delete child widgets, Qt does it all for us
}

namespace
{
// The options, beside the dimensioning mode, that decide which commands the
// Sketcher's tool bars carry (Workbench.cpp reads them when it builds them)
struct ToolBarOptions
{
    bool unifiedCoincident;
    bool autoHorVer;
    bool unifiedLines;

    static ToolBarOptions read()
    {
        ParameterGrp::handle constraints = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Constraints");
        ParameterGrp::handle commands = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Commands");
        return {constraints->GetBool("UnifiedCoincident", Sketcher::SketcherParams::defaultUnifiedCoincident()),
                constraints->GetBool("AutoHorVer", Sketcher::SketcherParams::defaultAutoHorVer()),
                commands->GetBool("UnifiedLineCommands", Sketcher::SketcherParams::defaultUnifiedLineCommands())};
    }

    bool operator==(const ToolBarOptions& other) const
    {
        return unifiedCoincident == other.unifiedCoincident && autoHorVer == other.autoHorVer
            && unifiedLines == other.unifiedLines;
    }
};

// Build the tool bars again after an option that decides what they carry
// has changed -- the workbench installing its UI once more, which is a good
// deal less than the restart upstream asks for (4f429e3288).
//
// The two bars these options decide are emptied first. The tool bar manager
// keeps what a bar has and adds a command it lacks at the END of the bar
// (it does not take buttons off and put them back, against flicker), so a
// group that replaces two buttons would land after everything else.
void reinstallToolBars()
{
    for (const char* name : {"Sketcher geometries", "Sketcher constraints"}) {
        if (auto* bar = Gui::getMainWindow()->findChild<QToolBar*>(QString::fromLatin1(name))) {
            bar->clear();
        }
    }
    if (auto* workbench = Gui::WorkbenchManager::instance()->active()) {
        workbench->activate();
    }
}
}  // namespace

void SketcherSettings::saveSettings()
{
    const ToolBarOptions previousToolBars = ToolBarOptions::read();

    // Sketch editing
    ui->checkBoxAdvancedSolverTaskBox->onSave();
    ui->checkBoxRecalculateInitialSolutionWhileDragging->onSave();
    ui->checkBoxEnableEscape->onSave();
    ui->checkBoxNotifyConstraintSubstitutions->onSave();
    ui->checkBoxAutoRemoveRedundants->onSave();
    ui->checkBoxMakeInternals->onSave();
    ui->checkBoxUnifiedCoincident->onSave();
    ui->checkBoxHorVerAuto->onSave();
    ui->checkBoxLineGroup->onSave();

    enum
    {
        DimensionSingleTool,
        DimensionSeparateTools,
        DimensionBoth
    };

    // Dimensioning constraints mode
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning");
    const bool previousSingleTool = hGrp->GetBool("SingleDimensioningTool", Sketcher::SketcherParams::defaultSingleDimensioningTool());
    const bool previousSeparatedTools = hGrp->GetBool("SeparatedDimensioningTools", Sketcher::SketcherParams::defaultSeparatedDimensioningTools());
    bool singleTool = true;
    bool SeparatedTools = false;
    int index = ui->dimensioningMode->currentIndex();
    switch (index) {
        case DimensionSeparateTools:
            singleTool = false;
            SeparatedTools = true;
            break;
        case DimensionBoth:
            singleTool = true;
            SeparatedTools = true;
            break;
    }
    const bool dimensioningChanged = singleTool != previousSingleTool
        || SeparatedTools != previousSeparatedTools;
    hGrp->SetBool("SingleDimensioningTool", singleTool);
    hGrp->SetBool("SeparatedDimensioningTools", SeparatedTools);

    // These two decide which dimensioning commands the Sketcher's toolbar and
    // menu carry, and they are read by Workbench::setupToolBars(), as are the
    // three options that group or split the coincident, horizontal/vertical
    // and line commands.
    if (dimensioningChanged || !(ToolBarOptions::read() == previousToolBars)) {
        reinstallToolBars();
    }

    ui->radiusDiameterMode->setEnabled(index != 1);

    enum
    {
        DimensionAutoRadiusDiam,
        DimensionDiameter,
        DimensionRadius
    };

    bool Diameter = true;
    bool Radius = true;
    index = ui->radiusDiameterMode->currentIndex();
    switch (index) {
        case DimensionDiameter:
            Diameter = true;
            Radius = false;
            break;
        case DimensionRadius:
            Diameter = false;
            Radius = true;
            break;
    }
    hGrp->SetBool("DimensioningDiameter", Diameter);
    hGrp->SetBool("DimensioningRadius", Radius);

    index = ui->autoScaleMode->currentIndex();
    hGrp->SetInt("AutoScaleMode", index);

    hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools");

    index = ui->ovpVisibility->currentIndex();
    hGrp->SetInt("OnViewParameterVisibility", index);
}

void SketcherSettings::loadSettings()
{
    // Sketch editing
    ui->checkBoxAdvancedSolverTaskBox->onRestore();
    ui->checkBoxRecalculateInitialSolutionWhileDragging->onRestore();
    ui->checkBoxEnableEscape->onRestore();
    ui->checkBoxNotifyConstraintSubstitutions->onRestore();
    ui->checkBoxAutoRemoveRedundants->onRestore();
    ui->checkBoxMakeInternals->onRestore();
    ui->checkBoxUnifiedCoincident->onRestore();
    ui->checkBoxHorVerAuto->onRestore();
    ui->checkBoxLineGroup->onRestore();

    // Dimensioning constraints mode.
    //
    // Signals stay blocked while the combo is rebuilt: clear() and the first
    // addItem() both emit currentIndexChanged, and loadSettings() runs again
    // every time the dialog reloads -- applying a preference pack does exactly
    // that. The connection is made once, with Qt::UniqueConnection, because it
    // used to be made here and so accumulated one duplicate per reload.
    {
        QSignalBlocker sigblk(ui->dimensioningMode);
        ui->dimensioningMode->clear();
        ui->dimensioningMode->addItem(tr("Single tool"));
        ui->dimensioningMode->addItem(tr("Separated tools"));
        ui->dimensioningMode->addItem(tr("Both"));
    }

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning");
    bool singleTool = hGrp->GetBool("SingleDimensioningTool", Sketcher::SketcherParams::defaultSingleDimensioningTool());
    bool SeparatedTools = hGrp->GetBool("SeparatedDimensioningTools", Sketcher::SketcherParams::defaultSeparatedDimensioningTools());
    int index = SeparatedTools ? (singleTool ? 2 : 1) : 0;
    {
        QSignalBlocker sigblk(ui->dimensioningMode);
        ui->dimensioningMode->setCurrentIndex(index);
    }
    connect(ui->dimensioningMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &SketcherSettings::dimensioningModeChanged,
            Qt::UniqueConnection);

    ui->radiusDiameterMode->setEnabled(index != 1);

    // Dimensioning constraints mode
    ui->radiusDiameterMode->clear();
    ui->radiusDiameterMode->addItem(tr("Auto"));
    ui->radiusDiameterMode->addItem(tr("Diameter"));
    ui->radiusDiameterMode->addItem(tr("Radius"));

    bool Diameter = hGrp->GetBool("DimensioningDiameter", Sketcher::SketcherParams::defaultDimensioningDiameter());
    bool Radius = hGrp->GetBool("DimensioningRadius", Sketcher::SketcherParams::defaultDimensioningRadius());
    index = Diameter ? (Radius ? 0 : 1) : 2;
    ui->radiusDiameterMode->setCurrentIndex(index);

    // The items have to be added in the same order as the AutoScaleMode enum
    ui->autoScaleMode->clear();
    ui->autoScaleMode->addItem(tr("Always"));
    ui->autoScaleMode->addItem(tr("Never"));
    ui->autoScaleMode->addItem(tr("When no scale feature is visible"));
    index = hGrp->GetInt("AutoScaleMode", Sketcher::SketcherParams::defaultAutoScaleMode());
    ui->autoScaleMode->setCurrentIndex(index);

    hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools");
    ui->ovpVisibility->clear();
    ui->ovpVisibility->addItem(tr("None"));
    ui->ovpVisibility->addItem(tr("Dimensions only"));
    ui->ovpVisibility->addItem(tr("Position and dimensions"));

    index = hGrp->GetInt("OnViewParameterVisibility", Sketcher::SketcherParams::defaultOnViewParameterVisibility());
    ui->ovpVisibility->setCurrentIndex(index);
}

void SketcherSettings::dimensioningModeChanged(int index)
{
    ui->radiusDiameterMode->setEnabled(index != 1);
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void SketcherSettings::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

void SketcherSettings::resetSettingsToDefaults()
{
    // What this page keeps outside its Gui::Pref* widgets: the combo boxes
    // are filled and stored by hand, so the base class does not know them
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning");
    const ToolBarOptions previousToolBars = ToolBarOptions::read();
    const bool previousSingleTool = hGrp->GetBool("SingleDimensioningTool", Sketcher::SketcherParams::defaultSingleDimensioningTool());
    const bool previousSeparatedTools = hGrp->GetBool("SeparatedDimensioningTools", Sketcher::SketcherParams::defaultSeparatedDimensioningTools());

    // the dimensioning tools on the tool bar
    hGrp->RemoveBool("SingleDimensioningTool");
    hGrp->RemoveBool("SeparatedDimensioningTools");

    // radius or diameter for the Dimension tool
    hGrp->RemoveBool("DimensioningDiameter");
    hGrp->RemoveBool("DimensioningRadius");

    hGrp->RemoveInt("AutoScaleMode");

    hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools");
    hGrp->RemoveInt("OnViewParameterVisibility");

    // and what the Gui::Pref* widgets keep
    PreferencePage::resetSettingsToDefaults();

    // The dialog makes a new page after this and never saves the old one, so
    // the tool bars follow here
    hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning");
    if (previousSingleTool != hGrp->GetBool("SingleDimensioningTool", Sketcher::SketcherParams::defaultSingleDimensioningTool())
        || previousSeparatedTools != hGrp->GetBool("SeparatedDimensioningTools", Sketcher::SketcherParams::defaultSeparatedDimensioningTools())
        || !(ToolBarOptions::read() == previousToolBars)) {
        reinstallToolBars();
    }
}

/* TRANSLATOR SketcherGui::SketcherSettingsGrid */

SketcherSettingsGrid::SketcherSettingsGrid(QWidget* parent)
    : PreferencePage(parent)
    , ui(new Ui_SketcherSettingsGrid)
{
    ui->setupUi(this);

    // The entries get their icons in event(): painted in the theme's text
    // colour, which is not known before the style has reached the page
    const auto lineStyleDelegate = new QStyledItemDelegate(this);
    ui->gridLinePattern->setIconSize(LineIconSize);
    ui->gridLinePattern->setItemDelegate(lineStyleDelegate);
    ui->gridDivLinePattern->setIconSize(LineIconSize);
    ui->gridDivLinePattern->setItemDelegate(lineStyleDelegate);
    for (auto style : PenStyles) {
        ui->gridLinePattern->addItem(QString(), QVariant(style.pattern));
        ui->gridDivLinePattern->addItem(QString(), QVariant(style.pattern));
    }
}

SketcherSettingsGrid::~SketcherSettingsGrid()
{
    // no need to delete child widgets, Qt does it all for us
}

bool SketcherSettingsGrid::event(QEvent* event)
{
    // StyleChange and not PaletteChange: without a style sheet the palette
    // change is never sent, the style change is, and by then the palette is
    // set either way (upstream a00fe1e886)
    if (event->type() == QEvent::StyleChange) {
        PreferencePage::event(event);
        const qreal dpr = devicePixelRatioF();
        const QBrush brush = palette().windowText();
        for (size_t i = 0; i < PenStyles.size(); ++i) {
            const QIcon icon = PenStyles[i].toIcon(LineIconSize, dpr, brush);
            ui->gridLinePattern->setItemIcon(static_cast<int>(i), icon);
            ui->gridDivLinePattern->setItemIcon(static_cast<int>(i), icon);
        }
        return true;
    }
    return PreferencePage::event(event);
}

void SketcherSettingsGrid::saveSettings()
{
    ui->checkBoxShowGrid->onSave();
    ui->gridSize->onSave();
    ui->checkBoxGridAuto->onSave();
    ui->gridSizePixelThreshold->onSave();
    ui->gridTransparency->onSave();
    ui->gridLineColor->onSave();
    ui->gridDivLineColor->onSave();
    ui->gridLineWidth->onSave();
    ui->gridDivLineWidth->onSave();
    ui->gridNumberSubdivision->onSave();

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/General");
    QVariant data = ui->gridLinePattern->itemData(ui->gridLinePattern->currentIndex());
    int pattern = data.toInt();
    hGrp->SetInt("GridLinePattern", pattern);

    data = ui->gridDivLinePattern->itemData(ui->gridDivLinePattern->currentIndex());
    pattern = data.toInt();
    hGrp->SetInt("GridDivLinePattern", pattern);
}

void SketcherSettingsGrid::loadSettings()
{
    ui->checkBoxShowGrid->onRestore();
    ui->gridSize->onRestore();
    ui->checkBoxGridAuto->onRestore();
    ui->gridSizePixelThreshold->onRestore();
    ui->gridTransparency->onRestore();
    ui->gridLineColor->onRestore();
    ui->gridDivLineColor->onRestore();
    ui->gridLineWidth->onRestore();
    ui->gridDivLineWidth->onRestore();
    ui->gridNumberSubdivision->onRestore();

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/General");
    int pattern = hGrp->GetInt("GridLinePattern", Sketcher::SketcherParams::defaultGridLinePattern());
    int index = ui->gridLinePattern->findData(QVariant(pattern));
    if (index < 0) {
        index = 1;
    }
    ui->gridLinePattern->setCurrentIndex(index);
    pattern = hGrp->GetInt("GridDivLinePattern", Sketcher::SketcherParams::defaultGridDivLinePattern());
    index = ui->gridDivLinePattern->findData(QVariant(pattern));
    if (index < 0) {
        index = 0;
    }
    ui->gridDivLinePattern->setCurrentIndex(index);
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void SketcherSettingsGrid::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

/* TRANSLATOR SketcherGui::SketcherSettingsDisplay */

SketcherSettingsDisplay::SketcherSettingsDisplay(QWidget* parent)
    : PreferencePage(parent)
    , ui(new Ui_SketcherSettingsDisplay)
{
    ui->setupUi(this);

    connect(ui->btnTVApply,
            &QPushButton::clicked,
            this,
            &SketcherSettingsDisplay::onBtnTVApplyClicked);
    connect(ui->fontBoxSketcherFontName,
            &QFontComboBox::currentFontChanged,
            this,
            &SketcherSettingsDisplay::onFontNameChanged);
    connect(ui->EditSketcherFontSize,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &SketcherSettingsDisplay::onFontSizeChanged);
}

/**
 *  Destroys the object and frees any allocated resources
 */
SketcherSettingsDisplay::~SketcherSettingsDisplay()
{
    // no need to delete child widgets, Qt does it all for us
}

void SketcherSettingsDisplay::saveSettings()
{
    ui->ZHeight->onSave();
    // A font box always holds some font. Stored only when it is another
    // than the page was loaded with, or one is stored already: unset means
    // the label's own font, and an OK on a page nobody touched must not
    // change what a label is drawn in. (Not a flag set by the box's
    // signal: that fires when the page is shown, too.)
    if (ui->fontBoxSketcherFontName->currentFont().family() != loadedFontFamily
        || !App::GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/View")
                ->GetASCII("EditSketcherFontName", Sketcher::SketcherParams::defaultEditSketcherFontName().c_str())
                .empty()) {
        ui->fontBoxSketcherFontName->onSave();
    }
    ui->EditSketcherFontSize->onSave();
    ui->ConstraintIconLabelsPerLine->onSave();
    ui->ConstraintIconLabelLines->onSave();
    ui->ElementIconSize->onSave();
    ui->axisTransparency->onSave();
    ui->ConstraintSymbolSize->onSave();
    ui->viewScalingFactor->onSave();
    ui->SegmentsPerGeometry->onSave();
    ui->dialogOnDistanceConstraint->onSave();
    ui->checkBoxEditDatumInPlace->onSave();
    ui->checkBoxDatumEscapeTakesBack->onSave();
    ui->checkBoxShowDirectionalAutoConstraintHints->onSave();
    ui->continueMode->onSave();
    ui->constraintMode->onSave();
    ui->checkBoxHideUnits->onSave();
    ui->checkBoxShowCursorCoords->onSave();
    ui->checkBoxUseSystemDecimals->onSave();
    ui->checkBoxShowDimensionalName->onSave();
    ui->prefDimensionalStringFormat->onSave();
    ui->checkBoxTVHideDependent->onSave();
    ui->checkBoxTVShowLinks->onSave();
    ui->checkBoxTVShowSupport->onSave();
    ui->checkBoxTVRestoreCamera->onSave();
    ui->checkBoxTVForceOrtho->onSave();
    ui->checkBoxTVSectionView->onSave();
    ui->checkBoxAdjustCamera->onSave();
    ui->checkBoxFitOnEdit->onSave();

}

void SketcherSettingsDisplay::loadSettings()
{
    ui->ZHeight->onRestore();
    ui->fontBoxSketcherFontName->onRestore();
    loadedFontFamily = ui->fontBoxSketcherFontName->currentFont().family();
    onFontNameChanged(ui->fontBoxSketcherFontName->currentFont());
    // Unset, or 0, both sizes are the application font's height: show
    // that, not the number the form was drawn with
    auto restoreFontHeightSize = [](Gui::PrefSpinBox* box) {
        const int height = QApplication::fontMetrics().height();
        box->setValue(height);
        box->onRestore();
        if (box->getWindowParameter()->GetInt(box->entryName(), height) <= 0) {
            QSignalBlocker block(box);
            box->setValue(height);
        }
    };
    restoreFontHeightSize(ui->EditSketcherFontSize);
    ui->ConstraintIconLabelsPerLine->onRestore();
    ui->ConstraintIconLabelLines->onRestore();
    ui->ElementIconSize->onRestore();
    ui->axisTransparency->onRestore();
    restoreFontHeightSize(ui->ConstraintSymbolSize);
    ui->viewScalingFactor->onRestore();
    ui->SegmentsPerGeometry->onRestore();
    ui->dialogOnDistanceConstraint->onRestore();
    ui->checkBoxEditDatumInPlace->onRestore();
    ui->checkBoxDatumEscapeTakesBack->onRestore();
    ui->checkBoxShowDirectionalAutoConstraintHints->onRestore();
    ui->continueMode->onRestore();
    ui->constraintMode->onRestore();
    ui->checkBoxHideUnits->onRestore();
    ui->checkBoxShowCursorCoords->onRestore();
    ui->checkBoxUseSystemDecimals->onRestore();
    ui->checkBoxShowDimensionalName->onRestore();
    ui->prefDimensionalStringFormat->onRestore();
    ui->checkBoxTVHideDependent->onRestore();
    ui->checkBoxTVShowLinks->onRestore();
    ui->checkBoxTVShowSupport->onRestore();
    ui->checkBoxTVRestoreCamera->onRestore();
    ui->checkBoxTVForceOrtho->onRestore();
    this->ui->checkBoxTVForceOrtho->setEnabled(this->ui->checkBoxTVRestoreCamera->isChecked());
    ui->checkBoxTVSectionView->onRestore();
    ui->checkBoxAdjustCamera->onRestore();
    ui->checkBoxFitOnEdit->onRestore();
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void SketcherSettingsDisplay::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

void SketcherSettingsDisplay::showEvent(QShowEvent* e)
{
    // the preview on the view's background, in a dimension's colour
    QPalette previewPalette = QPalette();
    previewPalette.setColor(QPalette::Window, getSketcherBackgroundColor());
    previewPalette.setColor(QPalette::WindowText, getSketcherConstraintColor());
    ui->LabelFontPreview->setPalette(previewPalette);

    Gui::Dialog::PreferencePage::showEvent(e);
}

void SketcherSettingsDisplay::onFontNameChanged(const QFont& font)
{
    QFont testFont;
    // For QFontMetrics::inFont() to say no, the style strategy has to be
    // set before the family
    testFont.setStyleStrategy(QFont::NoFontMerging);
    testFont.setFamily(font.family());

    QFontMetrics metrics(testFont);
    auto testChars = QString::fromUtf8(RequiredCharacters).toUcs4();

    QString missingChars;
    for (uint testChar : testChars) {
        if (!metrics.inFontUcs4(testChar)) {
            missingChars += QStringLiteral("  ");
            missingChars += QString::fromUcs4(reinterpret_cast<const char32_t*>(&testChar), 1);
        }
    }

    if (missingChars.length() > 0) {
        ui->LabelFontMessage->setText(tr("Glyphs not present:") + missingChars);
        ui->LabelFontMessage->show();
    }
    else {
        ui->LabelFontMessage->hide();
    }

    QFont previewFont(font);
    previewFont.setPixelSize(ui->EditSketcherFontSize->value());
    ui->LabelFontPreview->setFont(previewFont);
}

void SketcherSettingsDisplay::onFontSizeChanged(int size)
{
    QFont previewFont = ui->fontBoxSketcherFontName->currentFont();
    previewFont.setPixelSize(size);
    ui->LabelFontPreview->setFont(previewFont);
}

QColor SketcherSettingsDisplay::getSketcherBackgroundColor()
{
    auto parameters = App::GetApplication().GetUserParameter().GetGroup("BaseApp/Preferences/View");

    uint32_t backgroundColor;
    if (parameters->GetBool("Gradient", Gui::ViewParams::defaultGradient()) || parameters->GetBool("RadialGradient", Gui::ViewParams::defaultRadialGradient())) {
        if (parameters->GetBool("UseBackgroundColorMid", Gui::ViewParams::defaultUseBackgroundColorMid())) {
            backgroundColor = parameters->GetUnsigned("BackgroundColor4", Gui::ViewParams::defaultBackgroundColor4());
        }
        else {
            // a gradient of two colours: their average, the background in
            // the middle of the view
            backgroundColor = (((parameters->GetUnsigned("BackgroundColor2", Gui::ViewParams::defaultBackgroundColor2())) >> 8)
                               + ((parameters->GetUnsigned("BackgroundColor3", Gui::ViewParams::defaultBackgroundColor3())) >> 8))
                << 7;
        }
    }
    else {
        backgroundColor = parameters->GetUnsigned("BackgroundColor", Gui::ViewParams::defaultBackgroundColor());
    }

    return QColor((backgroundColor >> 24) & 0xFF,
                  (backgroundColor >> 16) & 0xFF,
                  (backgroundColor >> 8) & 0xFF);
}

QColor SketcherSettingsDisplay::getSketcherConstraintColor()
{
    auto parameters = App::GetApplication().GetUserParameter().GetGroup("BaseApp/Preferences/View");
    uint32_t constraintColor = parameters->GetUnsigned("ConstrainedDimColor", Sketcher::SketcherParams::defaultConstrainedDimColor());

    return QColor((constraintColor >> 24) & 0xFF,
                  (constraintColor >> 16) & 0xFF,
                  (constraintColor >> 8) & 0xFF);
}

void SketcherSettingsDisplay::onBtnTVApplyClicked(bool)
{
    QString errMsg;
    try {
        Gui::Command::doCommand(Gui::Command::Gui,
                                "for name,doc in App.listDocuments().items():\n"
                                "    for sketch in doc.findObjects('Sketcher::SketchObject'):\n"
                                "        sketch.ViewObject.HideDependent = %s\n"
                                "        sketch.ViewObject.ShowLinks = %s\n"
                                "        sketch.ViewObject.ShowSupport = %s\n"
                                "        sketch.ViewObject.RestoreCamera = %s\n"
                                "        sketch.ViewObject.ForceOrtho = %s\n"
                                "        sketch.ViewObject.SectionView = %s\n",
                                this->ui->checkBoxTVHideDependent->isChecked() ? "True" : "False",
                                this->ui->checkBoxTVShowLinks->isChecked() ? "True" : "False",
                                this->ui->checkBoxTVShowSupport->isChecked() ? "True" : "False",
                                this->ui->checkBoxTVRestoreCamera->isChecked() ? "True" : "False",
                                this->ui->checkBoxTVForceOrtho->isChecked() ? "True" : "False",
                                this->ui->checkBoxTVSectionView->isChecked() ? "True" : "False");
    }
    catch (Base::PyException& e) {
        Base::Console().DeveloperError("SketcherSettings", "error in onBtnTVApplyClicked:\n");
        e.ReportException();
        errMsg = QString::fromUtf8(e.what());
    }
    catch (...) {
        errMsg = tr("Unexpected C++ exception");
    }
    if (errMsg.length() > 0) {
        QMessageBox::warning(this, tr("Sketcher"), errMsg);
    }
}


/* TRANSLATOR SketcherGui::SketcherSettingsAppearance */

namespace
{
// A line type combo box of the appearance page: its preference in
// Mod/Sketcher/View and the pattern drawn when that is not set
struct LinePatternBox
{
    QComboBox* box;
    const char* entry;
    int defaultPattern;
};

std::array<LinePatternBox, 8> linePatternBoxes(Ui_SketcherSettingsAppearance* ui)
{
    return {{
        {ui->EdgePattern, "EdgePattern", 0b1111111111111111},
        {ui->ConstructionPattern, "ConstructionPattern", 0b1111110011111100},
        {ui->InternalPattern, "InternalPattern", 0b1111110011111100},
        {ui->ExternalPattern, "ExternalPattern", 0b1111110011111100},
        {ui->ExternalDefiningPattern, "ExternalDefiningPattern", 0b1111111111111111},
        {ui->InformationPattern, "InformationPattern", 0b1111110011111100},
        {ui->DimensionalConstraintLinePattern,
         "DimensionalConstraintLinePattern",
         0b1111111111111111},
        {ui->AxisLinePattern, "AxisLinePattern", 0b1111111111111111},
    }};
}

const char* const SketcherViewGroup = "User parameter:BaseApp/Preferences/Mod/Sketcher/View";
}  // namespace

SketcherSettingsAppearance::SketcherSettingsAppearance(QWidget* parent)
    : PreferencePage(parent)
    , ui(new Ui_SketcherSettingsAppearance)
{
    ui->setupUi(this);

    // The entries get their icons in event(), as the grid page's do
    const auto lineStyleDelegate = new QStyledItemDelegate(this);
    for (const auto& line : linePatternBoxes(ui.get())) {
        line.box->setIconSize(LineIconSize);
        line.box->setItemDelegate(lineStyleDelegate);
        for (auto style : PenStyles) {
            line.box->addItem(QString(), QVariant(style.pattern));
        }
    }
}

/**
 *  Destroys the object and frees any allocated resources
 */
SketcherSettingsAppearance::~SketcherSettingsAppearance()
{
    // no need to delete child widgets, Qt does it all for us
}

bool SketcherSettingsAppearance::event(QEvent* event)
{
    // Painted from the page's own palette once the style has reached it,
    // as on the grid page. Upstream (efec2c6795) shows a label of its own
    // to read the style sheet's colour from; a styled page already has it.
    if (event->type() == QEvent::StyleChange) {
        PreferencePage::event(event);
        const qreal dpr = devicePixelRatioF();
        const QBrush brush = palette().windowText();
        for (size_t i = 0; i < PenStyles.size(); ++i) {
            const QIcon icon = PenStyles[i].toIcon(LineIconSize, dpr, brush);
            for (const auto& line : linePatternBoxes(ui.get())) {
                line.box->setItemIcon(static_cast<int>(i), icon);
            }
        }
        return true;
    }
    return PreferencePage::event(event);
}

void SketcherSettingsAppearance::saveSettings()
{
    // Sketcher
    ui->SketchEdgeColor->onSave();
    ui->SketchVertexColor->onSave();
    ui->EditedEdgeColor->onSave();
    ui->ConstructionColor->onSave();
    ui->ExternalColor->onSave();
    ui->ExternalDefiningColor->onSave();
    ui->InvalidSketchColor->onSave();
    ui->FullyConstrainedColor->onSave();
    ui->InternalAlignedGeoColor->onSave();
    ui->FullyConstraintElementColor->onSave();
    ui->FullyConstraintConstructionElementColor->onSave();
    ui->FullyConstraintInternalAlignmentColor->onSave();
    ui->InformationColor->onSave();
    ui->GridLineColor->onSave();

    ui->FrozenColor->onSave();
    ui->DetachedColor->onSave();
    ui->MissingColor->onSave();

    ui->InternalFaceColor->onSave();

    ui->ConstrainedColor->onSave();
    ui->NonDrivingConstraintColor->onSave();
    ui->DatumColor->onSave();
    ui->ExprBasedConstrDimColor->onSave();
    ui->DeactivatedConstrDimColor->onSave();

    ui->CursorTextColor->onSave();
    ui->CursorCrosshairColor->onSave();

    ui->EdgeWidth->onSave();
    ui->ConstructionWidth->onSave();
    ui->InternalWidth->onSave();
    ui->ExternalWidth->onSave();
    ui->ExternalDefiningWidth->onSave();
    ui->InformationWidth->onSave();
    ui->DimensionalConstraintLineWidth->onSave();
    ui->AxisLineWidth->onSave();

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(SketcherViewGroup);
    for (const auto& line : linePatternBoxes(ui.get())) {
        hGrp->SetInt(line.entry, line.box->itemData(line.box->currentIndex()).toInt());
    }
}

void SketcherSettingsAppearance::loadSettings()
{
    // Sketcher
    ui->SketchEdgeColor->onRestore();
    ui->SketchVertexColor->onRestore();
    ui->EditedEdgeColor->onRestore();
    ui->ConstructionColor->onRestore();
    ui->ExternalColor->onRestore();
    ui->ExternalDefiningColor->onRestore();
    ui->InvalidSketchColor->onRestore();
    ui->FullyConstrainedColor->onRestore();
    ui->InternalAlignedGeoColor->onRestore();
    ui->FullyConstraintElementColor->onRestore();
    ui->FullyConstraintConstructionElementColor->onRestore();
    ui->FullyConstraintInternalAlignmentColor->onRestore();
    ui->InformationColor->onRestore();
    ui->GridLineColor->onRestore();

    ui->FrozenColor->onRestore();
    ui->DetachedColor->onRestore();
    ui->MissingColor->onRestore();

    ui->InternalFaceColor->setAllowTransparency(true);
    ui->InternalFaceColor->onRestore();

    ui->ConstrainedColor->onRestore();
    ui->NonDrivingConstraintColor->onRestore();
    ui->DatumColor->onRestore();
    ui->ExprBasedConstrDimColor->onRestore();
    ui->DeactivatedConstrDimColor->onRestore();

    ui->CursorTextColor->onRestore();
    ui->CursorCrosshairColor->onRestore();

    ui->EdgeWidth->onRestore();
    ui->ConstructionWidth->onRestore();
    ui->InternalWidth->onRestore();
    ui->ExternalWidth->onRestore();
    ui->ExternalDefiningWidth->onRestore();
    ui->InformationWidth->onRestore();
    ui->DimensionalConstraintLineWidth->onRestore();
    ui->AxisLineWidth->onRestore();

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(SketcherViewGroup);
    for (const auto& line : linePatternBoxes(ui.get())) {
        int index = line.box->findData(QVariant(int(hGrp->GetInt(line.entry, line.defaultPattern))));
        line.box->setCurrentIndex(index < 0 ? 0 : index);
    }
}

void SketcherSettingsAppearance::resetSettingsToDefaults()
{
    // the line types are stored by hand, so the base class does not know them
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(SketcherViewGroup);
    for (const auto& line : linePatternBoxes(ui.get())) {
        hGrp->RemoveInt(line.entry);
    }
    PreferencePage::resetSettingsToDefaults();
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void SketcherSettingsAppearance::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

#include "moc_SketcherSettings.cpp"
