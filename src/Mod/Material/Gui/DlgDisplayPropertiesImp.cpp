// SPDX-License-Identifier: LGPL-2.1-or-later

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

#include <QAbstractItemView>
#include <QCompleter>
#include <QEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <algorithm>
#include <fastsignals/signal.h>

#include <Base/Console.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/GeoFeature.h>
#include <App/ShaderObject.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/DlgMaterialPropertiesImp.h>
#include <Gui/DockWindowManager.h>
#include <Gui/PrefWidgets.h>
#include <Gui/Document.h>
#include <Gui/Selection/Selection.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Gui/ViewProviderGeometryObject.h>
#include <Gui/WaitCursor.h>

#include <Mod/Material/App/Exceptions.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/ModelUuids.h>
#include <Mod/Material/App/PropertyMaterial.h>
#include <Mod/Material/App/ShaderGraph.h>

#include "DlgDisplayPropertiesImp.h"
#include "MaterialIcons.h"
#include "ui_DlgDisplayProperties.h"


using namespace MatGui;
using namespace std;
namespace sp = std::placeholders;


/* TRANSLATOR Gui::Dialog::DlgDisplayPropertiesImp */

class DlgDisplayPropertiesImp::Private
{
    using DlgDisplayPropertiesImp_Connection = fastsignals::connection;

public:
    Ui::DlgDisplayProperties ui;
    /// The finish presets, and the completer that offers them on the
    /// finish line edit. One row per pattern: the label to type, the
    /// rendered icon to recognise it by, and the pattern id under
    /// Qt::UserRole.
    QStandardItemModel* finishModel = nullptr;
    QCompleter* finishCompleter = nullptr;
    /// What the box held before the last keystroke, which is how a
    /// deletion is told from an insertion -- see inlineCompleteFinish().
    QString finishTyped;
    DlgDisplayPropertiesImp_Connection connectChangedObject;
    DlgDisplayPropertiesImp_Connection connectDeletedObject;
    DlgDisplayPropertiesImp_Connection connectDeleteDocument;
    /// document name, object name -- never a view provider pointer, see getTargets()
    std::vector<std::pair<std::string, std::string>> targets;

    static void setElementColor(const std::vector<Gui::ViewProvider*>& views,
                                const char* property,
                                Gui::ColorButton* buttonColor)
    {
        bool hasElementColor = false;
        for (const auto& view : views) {
            if (auto* prop = dynamic_cast<App::PropertyColor*>(view->getPropertyByName(property))) {
                Base::Color color = prop->getValue();
                QSignalBlocker block(buttonColor);
                buttonColor->setColor(color.asValue<QColor>());
                hasElementColor = true;
                break;
            }
        }

        buttonColor->setEnabled(hasElementColor);
    }

    static void setElementAppearance(const std::vector<Gui::ViewProvider*>& views,
                                     const char* property,
                                     Gui::ColorButton* buttonColor)
    {
        bool hasElementColor = false;
        for (const auto& view : views) {
            if (auto* prop =
                    dynamic_cast<App::PropertyAppearance*>(view->getPropertyByName(property))) {
                // This fork's PropertyMaterial has no per-field getters.
                Base::Color color = prop->getValue().diffuseColor;
                QSignalBlocker block(buttonColor);
                buttonColor->setColor(color.asValue<QColor>());
                hasElementColor = true;
                break;
            }
        }

        buttonColor->setEnabled(hasElementColor);
    }

    static void setDrawStyle(const std::vector<Gui::ViewProvider*>& views,
                             const char* property,
                             QDoubleSpinBox* spinbox)
    {
        bool hasDrawStyle = false;
        for (const auto& view : views) {
            if (auto* prop = dynamic_cast<App::PropertyFloat*>(view->getPropertyByName(property))) {
                QSignalBlocker block(spinbox);
                spinbox->setValue(prop->getValue());
                hasDrawStyle = true;
                break;
            }
        }

        spinbox->setEnabled(hasDrawStyle);
    }

    static void setPropertyBool(const std::vector<Gui::ViewProvider*>& views,
                                const char* name,
                                QCheckBox* checkbox)
    {
        bool enable = false;
        for (const auto& view : views) {
            if (auto prop =
                    Base::freecad_dynamic_cast<App::PropertyBool>(view->getPropertyByName(name))) {
                QSignalBlocker guard(checkbox);
                checkbox->setChecked(prop->getValue());
                enable = true;
                break;
            }
        }

        checkbox->setEnabled(enable);
    }

    static void setTransparency(const std::vector<Gui::ViewProvider*>& views,
                                const char* property,
                                QSpinBox* spinbox,
                                QSlider* slider)
    {
        bool hasTransparency = false;
        for (const auto& view : views) {
            if (auto* prop =
                    dynamic_cast<App::PropertyInteger*>(view->getPropertyByName(property))) {
                QSignalBlocker blockSpinBox(spinbox);
                spinbox->setValue(prop->getValue());

                QSignalBlocker blockSlider(slider);
                slider->setValue(prop->getValue());
                hasTransparency = true;
                break;
            }
        }

        spinbox->setEnabled(hasTransparency);
        slider->setEnabled(hasTransparency);
    }
};

DlgDisplayPropertiesImp::DlgDisplayPropertiesImp(QWidget* parent, Qt::WindowFlags fl)
    : QDialog(parent, fl)
    , d(new Private)
{
    d->ui.setupUi(this);
    setupConnections();

    d->ui.textLabel1_3->hide();
    d->ui.changePlot->hide();
    d->ui.buttonLineColor->setModal(false);
    d->ui.buttonPointColor->setModal(false);
    d->ui.buttonColor->setModal(false);

    d->ui.checkBoxColorRecompute->initAutoSave();

    // Create a filter to only include current format materials
    // that contain the basic render model.
    setupFilters();
    setupFinishPresets();

    {
        QSignalBlocker block(d->ui.widgetMaterial);
        rememberTargets(getSelection());
        setPropertiesFromSelection();
    }

    Gui::SelectionRoom().Attach(this);

    // NOLINTBEGIN
    d->connectChangedObject = Gui::Application::Instance->signalChangedObject.connect(
        std::bind(&DlgDisplayPropertiesImp::slotChangedObject, this, sp::_1, sp::_2));
    d->connectDeletedObject = Gui::Application::Instance->signalDeletedObject.connect(
        std::bind(&DlgDisplayPropertiesImp::slotDeletedObject, this, sp::_1));
    d->connectDeleteDocument = Gui::Application::Instance->signalDeleteDocument.connect(
        std::bind(&DlgDisplayPropertiesImp::slotDeleteDocument, this, sp::_1));
    // NOLINTEND
}

DlgDisplayPropertiesImp::~DlgDisplayPropertiesImp()
{
    // no need to delete child widgets, Qt does it all for us
    d->connectChangedObject.disconnect();
    d->connectDeletedObject.disconnect();
    d->connectDeleteDocument.disconnect();
    Gui::SelectionRoom().Detach(this);
}

namespace
{
/// What the synthetic top row of the look list carries where a card
/// carries its UUID. Not a UUID and never written to a document -- it
/// only has to be a string no card can answer to.
const QString& asMaterialId()
{
    static const QString id = QStringLiteral("as-material");
    return id;
}
}  // namespace

void DlgDisplayPropertiesImp::setupFilters()
{
    // Create a filter to only include current format materials
    // that contain the basic render model.
    auto filterList = std::make_shared<std::list<std::shared_ptr<Materials::MaterialFilter>>>();

    auto filter = std::make_shared<Materials::MaterialFilter>();
    filter->setName(tr("Basic appearance"));
    filter->addRequiredComplete(Materials::ModelUUIDs::ModelUUID_Rendering_Basic);
    filterList->push_back(filter);

    filter = std::make_shared<Materials::MaterialFilter>();
    filter->setName(tr("Texture appearance"));
    filter->addRequiredComplete(Materials::ModelUUIDs::ModelUUID_Rendering_Texture);
    filterList->push_back(filter);
    // Cards shaded by a MaterialX shader graph (docs/MaterialStorage.md sec 17)
    filter = std::make_shared<Materials::MaterialFilter>();
    filter->setName(tr("MaterialX appearance"));
    filter->addRequiredComplete(Materials::ModelUUIDs::ModelUUID_Rendering_MaterialX);
    filterList->push_back(filter);

    // Deliberately no "All materials" tab: this is a LOOK picker, and
    // physical cards and hatch patterns have no look to offer
    // (docs/MaterialStorage.md 15.5).
    d->ui.widgetMaterial->setIncludeEmptyFolders(false);
    d->ui.widgetMaterial->setIncludeLegacy(false);

    d->ui.widgetMaterial->setFilter(filterList);
    // The first entry is the way back to the object's card, so the list
    // says what the status line under it says (docs/MaterialStorage.md
    // 15.5). It is the same action as the Reset to material button.
    d->ui.widgetMaterial->setLeadingEntry(asMaterialId(),
                                          tr("As material"),
                                          tr("Take the look from the object's material card, "
                                             "and keep taking it when the card changes"));

    // The picker at the top is the Material panel's, filter and all: it
    // edits the object's CARD, which is what mass, FEM and CAM read
    Materials::MaterialFilter cards;
    cards.requirePhysical(true);
    d->ui.widgetCard->setFilter(cards);
}

void DlgDisplayPropertiesImp::setupConnections()
{
    connect(d->ui.changeMode,
            &QComboBox::textActivated,
            this,
            &DlgDisplayPropertiesImp::onChangeModeActivated);
    connect(d->ui.changePlot,
            &QComboBox::textActivated,
            this,
            &DlgDisplayPropertiesImp::onChangePlotActivated);
    connect(d->ui.spinTransparency,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &DlgDisplayPropertiesImp::onSpinTransparencyValueChanged);
    connect(d->ui.spinPointSize,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &DlgDisplayPropertiesImp::onSpinPointSizeValueChanged);
    connect(d->ui.buttonColor,
            &Gui::ColorButton::changed,
            this,
            &DlgDisplayPropertiesImp::onButtonColorChanged);
    connect(d->ui.buttonLineColor,
            &Gui::ColorButton::changed,
            this,
            &DlgDisplayPropertiesImp::onButtonLineColorChanged);
    connect(d->ui.buttonPointColor,
            &Gui::ColorButton::changed,
            this,
            &DlgDisplayPropertiesImp::onButtonPointColorChanged);
    connect(d->ui.spinLineWidth,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &DlgDisplayPropertiesImp::onSpinLineWidthValueChanged);
    connect(d->ui.spinLineTransparency,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &DlgDisplayPropertiesImp::onSpinLineTransparencyValueChanged);
    connect(d->ui.buttonCustomAppearance,
            &Gui::ColorButton::clicked,
            this,
            &DlgDisplayPropertiesImp::onButtonCustomAppearanceClicked);
    connect(d->ui.buttonColorPlot,
            &Gui::ColorButton::clicked,
            this,
            &DlgDisplayPropertiesImp::onButtonColorPlotClicked);
    connect(d->ui.widgetMaterial,
            &MaterialTreeWidget::materialSelected,
            this,
            &DlgDisplayPropertiesImp::onMaterialSelected);
    connect(d->ui.widgetCard,
            &MaterialTreeWidget::materialSelected,
            this,
            &DlgDisplayPropertiesImp::onCardSelected);
    connect(d->ui.buttonResetToMaterial,
            &QPushButton::clicked,
            this,
            &DlgDisplayPropertiesImp::onResetToMaterial);
    connect(d->ui.buttonEditShaderGraph,
            &QPushButton::clicked,
            this,
            &DlgDisplayPropertiesImp::onEditShaderGraph);
    // The list's own way to the same place
    connect(d->ui.widgetMaterial,
            &MaterialTreeWidget::leadingEntrySelected,
            this,
            &DlgDisplayPropertiesImp::onResetToMaterial);
    // The other way into the same slot -- a name typed and committed --
    // is wired here; the completer does not exist yet at this point and
    // connects itself in setupFinishPresets(). editingFinished also
    // catches the clear button, which empties the box: that is None.
    connect(d->ui.editFinish, &QLineEdit::editingFinished, this, [this]() {
        onFinishPresetActivated(d->ui.editFinish->text());
    });
    // Growing text gets the rest of the name written for it; shrinking
    // text does not, or Backspace would be undone as fast as it is
    // pressed.
    connect(d->ui.editFinish, &QLineEdit::textEdited, this, [this](const QString& text) {
        const bool grew = text.length() > d->finishTyped.length();
        d->finishTyped = text;
        if (grew) {
            inlineCompleteFinish(text);
        }
    });
    connect(d->ui.spinFinishPitch,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &DlgDisplayPropertiesImp::onFinishSizeChanged);
    connect(d->ui.spinFinishDepth,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &DlgDisplayPropertiesImp::onFinishSizeChanged);
    connect(d->ui.spinFinishAngle,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &DlgDisplayPropertiesImp::onFinishSizeChanged);
    connect(d->ui.checkBoxMapFaceColor,
            &QCheckBox::toggled,
            this,
            &DlgDisplayPropertiesImp::onMapFaceColorChanged);
    connect(d->ui.checkBoxMapLineColor,
            &QCheckBox::toggled,
            this,
            &DlgDisplayPropertiesImp::onMapLineColorChanged);
    connect(d->ui.checkBoxMapPointColor,
            &QCheckBox::toggled,
            this,
            &DlgDisplayPropertiesImp::onMapPointColorChanged);
    connect(d->ui.checkBoxMapTransparency,
            &QCheckBox::toggled,
            this,
            &DlgDisplayPropertiesImp::onMapTransparencyChanged);
}

void DlgDisplayPropertiesImp::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        d->ui.retranslateUi(this);
    }
    QDialog::changeEvent(e);
}

void DlgDisplayPropertiesImp::setPropertiesFromSelection()
{
    std::vector<Gui::ViewProvider*> views = getTargets();
    setDisplayModes(views);
    setColorPlot(views);
    setMaterialCard(views);
    setShapeAppearance(views);
    setShapeFinish(views);
    setShapeColor(views);
    setLineColor(views);
    setPointColor(views);
    setPointSize(views);
    setLineWidth(views);
    setTransparency(views);
    setLineTransparency(views);
    setMapFaceColor(views);
    setMapEdgeColor(views);
    setMapVertexColor(views);
    setMapTransparency(views);
}

/// @cond DOXERR
void DlgDisplayPropertiesImp::OnChange(Gui::SelectionSingleton::SubjectType& rCaller,
                                       Gui::SelectionSingleton::MessageType Reason)
{
    Q_UNUSED(rCaller);
    if (Reason.Type == Gui::SelectionChanges::AddSelection
        || Reason.Type == Gui::SelectionChanges::RmvSelection
        || Reason.Type == Gui::SelectionChanges::SetSelection
        || Reason.Type == Gui::SelectionChanges::ClrSelection) {
        // An empty selection is not a reason to stop editing: keep the objects
        // the dialog was opened on, so clearing the selection -- or picking in
        // the 3D view -- does not silently disarm every control.
        std::vector<Gui::ViewProvider*> views = getSelection();
        if (!views.empty()) {
            rememberTargets(views);
        }
        setPropertiesFromSelection();
    }
}
/// @endcond

void DlgDisplayPropertiesImp::slotChangedObject(const Gui::ViewProvider& obj,
                                                const App::Property& prop)
{
    // This method gets called if a property of any view provider is changed.
    // We pick out all the properties for which we need to update this dialog.
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    auto vp = std::find_if(Provider.begin(), Provider.end(), [&obj](Gui::ViewProvider* v) {
        return v == &obj;
    });

    if (vp != Provider.end()) {
        const char* name = obj.getPropertyName(&prop);
        // this is not a property of the view provider but of the document object
        if (!name) {
            return;
        }
        std::string prop_name = name;
        if (prop.is<App::PropertyColor>()) {
            Base::Color value = static_cast<const App::PropertyColor&>(prop).getValue();
            if (prop_name == "ShapeColor") {
                bool blocked = d->ui.buttonColor->blockSignals(true);
                d->ui.buttonColor->setColor(value.asValue<QColor>());
                d->ui.buttonColor->blockSignals(blocked);
            }
            else if (prop_name == "LineColor") {
                bool blocked = d->ui.buttonLineColor->blockSignals(true);
                d->ui.buttonLineColor->setColor(value.asValue<QColor>());
                d->ui.buttonLineColor->blockSignals(blocked);
            }
            else if (prop_name == "PointColor") {
                bool blocked = d->ui.buttonPointColor->blockSignals(true);
                d->ui.buttonPointColor->setColor(value.asValue<QColor>());
                d->ui.buttonPointColor->blockSignals(blocked);
            }
        }
        else if (prop.isDerivedFrom<App::PropertyAppearanceList>()) {
            if (prop_name == "ShapeAppearance") {
                // The look, the list's selection, the status line and the
                // Reset button all move together, and setMaterialCard is
                // what moves them.
                setMaterialCard(getTargets());
            }
        }
        else if (prop.isDerivedFrom<App::PropertyInteger>()) {
            long value = static_cast<const App::PropertyInteger&>(prop).getValue();
            if (prop_name == "Transparency") {
                bool blocked = d->ui.spinTransparency->blockSignals(true);
                d->ui.spinTransparency->setValue(value);
                d->ui.spinTransparency->blockSignals(blocked);
                blocked = d->ui.horizontalSlider->blockSignals(true);
                d->ui.horizontalSlider->setValue(value);
                d->ui.horizontalSlider->blockSignals(blocked);
            }
            else if (prop_name == "LineTransparency") {
                bool blocked = d->ui.spinLineTransparency->blockSignals(true);
                d->ui.spinLineTransparency->setValue(value);
                d->ui.spinLineTransparency->blockSignals(blocked);
                blocked = d->ui.sliderLineTransparency->blockSignals(true);
                d->ui.sliderLineTransparency->setValue(value);
                d->ui.sliderLineTransparency->blockSignals(blocked);
            }
        }
        else if (prop.isDerivedFrom<App::PropertyFloat>()) {
            double value = static_cast<const App::PropertyFloat&>(prop).getValue();
            if (prop_name == "PointSize") {
                bool blocked = d->ui.spinPointSize->blockSignals(true);
                d->ui.spinPointSize->setValue(value);
                d->ui.spinPointSize->blockSignals(blocked);
            }
            else if (prop_name == "LineWidth") {
                bool blocked = d->ui.spinLineWidth->blockSignals(true);
                d->ui.spinLineWidth->setValue(value);
                d->ui.spinLineWidth->blockSignals(blocked);
            }
        }
    }
}

void DlgDisplayPropertiesImp::reject()
{
    QDialog::reject();
}

/**
 * Opens a dialog that allows one to modify the 'ShapeMaterial' property of all selected view providers.
 */
void DlgDisplayPropertiesImp::onButtonCustomAppearanceClicked()
{
    // This fork's dialog is not an in/out material editor: it is given the
    // name of the property and the view providers, edits them live so the 3D
    // view answers as controls move, and restores its own snapshot on Cancel.
    // So there is nothing to seed beforehand or write back afterwards.
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    Gui::Dialog::DlgMaterialPropertiesImp dlg("ShapeAppearance", this);
    dlg.setViewProviders(Provider);
    // Cancel restores the appearance itself, and the ShapeColor mirror's
    // change event has already resynced the button by then
    if (dlg.exec() == QDialog::Accepted) {
        d->ui.buttonColor->setColor(dlg.diffuseColor());
    }
}

/**
 * Opens a dialog that allows one to modify the 'ShapeMaterial' property of all selected view providers.
 */
void DlgDisplayPropertiesImp::onButtonColorPlotClicked()
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    static QPointer<Gui::Dialog::DlgMaterialPropertiesImp> dlg = nullptr;
    if (!dlg) {
        dlg = new Gui::Dialog::DlgMaterialPropertiesImp("TextureMaterial", this);
    }
    dlg->setModal(false);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setViewProviders(Provider);
    dlg->show();
}

/**
 * Sets the 'Display' property of all selected view providers.
 */
void DlgDisplayPropertiesImp::onChangeModeActivated(const QString& s)
{
    Gui::WaitCursor wc;
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    for (auto it : Provider) {
        if (auto* prop =
                dynamic_cast<App::PropertyEnumeration*>(it->getPropertyByName("DisplayMode"))) {
            prop->setValue(static_cast<const char*>(s.toLatin1()));
        }
    }
}

void DlgDisplayPropertiesImp::onChangePlotActivated(const QString& s)
{
    Base::Console().log("Plot = %s\n", (const char*)s.toLatin1());
}

/**
 * Sets the 'Transparency' property of all selected view providers.
 */
void DlgDisplayPropertiesImp::onSpinTransparencyValueChanged(int transparency)
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    for (auto it : Provider) {
        if (auto* prop =
                dynamic_cast<App::PropertyInteger*>(it->getPropertyByName("Transparency"))) {
            prop->setValue(transparency);
        }
    }
}

/**
 * Sets the 'PointSize' property of all selected view providers.
 */
void DlgDisplayPropertiesImp::onSpinPointSizeValueChanged(double pointsize)
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    for (auto it : Provider) {
        if (auto* prop = dynamic_cast<App::PropertyFloat*>(it->getPropertyByName("PointSize"))) {
            prop->setValue(pointsize);
        }
    }
}

/**
 * Sets the 'ShapeColor' property of all selected view providers.
 */
void DlgDisplayPropertiesImp::onButtonColorChanged()
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    Base::Color c {};
    c.setValue<QColor>(d->ui.buttonColor->color());
    for (auto it : Provider) {
        if (auto* prop = dynamic_cast<App::PropertyColor*>(it->getPropertyByName("ShapeColor"))) {
            prop->setValue(c);
        }
    }
}

/**
 * Sets the 'LineWidth' property of all selected view providers.
 */
void DlgDisplayPropertiesImp::onSpinLineWidthValueChanged(double linewidth)
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    for (auto it : Provider) {
        if (auto* prop = dynamic_cast<App::PropertyFloat*>(it->getPropertyByName("LineWidth"))) {
            prop->setValue(linewidth);
        }
    }
}

void DlgDisplayPropertiesImp::onButtonLineColorChanged()
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    QColor s = d->ui.buttonLineColor->color();
    Base::Color c {};
    c.setValue<QColor>(s);
    for (auto it : Provider) {
        if (auto* prop = dynamic_cast<App::PropertyColor*>(it->getPropertyByName("LineColor"))) {
            prop->setValue(c);
        }
    }
}

void DlgDisplayPropertiesImp::onButtonPointColorChanged()
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    QColor s = d->ui.buttonPointColor->color();
    Base::Color c {};
    c.setValue<QColor>(s);
    for (auto it : Provider) {
        if (auto* prop = dynamic_cast<App::PropertyColor*>(it->getPropertyByName("PointColor"))) {
            prop->setValue(c);
        }
    }
}

void DlgDisplayPropertiesImp::onSpinLineTransparencyValueChanged(int transparency)
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    for (auto it : Provider) {
        if (auto* prop =
                dynamic_cast<App::PropertyInteger*>(it->getPropertyByName("LineTransparency"))) {
            prop->setValue(transparency);
        }
    }
}

void DlgDisplayPropertiesImp::onPropertyBoolChanged(const char* name, bool checked)
{
    for (auto vp : getTargets()) {
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(vp->getPropertyByName(name))) {
            prop->setValue(checked);
        }
    }
}

void DlgDisplayPropertiesImp::onMapFaceColorChanged(bool checked)
{
    onPropertyBoolChanged("MapFaceColor", checked);
}

void DlgDisplayPropertiesImp::onMapLineColorChanged(bool checked)
{
    onPropertyBoolChanged("MapEdgeColor", checked);
}

void DlgDisplayPropertiesImp::onMapPointColorChanged(bool checked)
{
    onPropertyBoolChanged("MapVertexColor", checked);
}

void DlgDisplayPropertiesImp::onMapTransparencyChanged(bool checked)
{
    onPropertyBoolChanged("MapTransparency", checked);
}

void DlgDisplayPropertiesImp::setDisplayModes(const std::vector<Gui::ViewProvider*>& views)
{
    QStringList commonModes;
    QStringList modes;
    for (auto it = views.begin(); it != views.end(); ++it) {
        if (auto* prop =
                dynamic_cast<App::PropertyEnumeration*>((*it)->getPropertyByName("DisplayMode"))) {
            if (!prop->hasEnums()) {
                return;
            }
            std::vector<std::string> value = prop->getEnumVector();
            if (it == views.begin()) {
                for (const auto& jt : value) {
                    commonModes << QLatin1String(jt.c_str());
                }
            }
            else {
                for (const auto& jt : value) {
                    if (commonModes.contains(QLatin1String(jt.c_str()))) {
                        modes << QLatin1String(jt.c_str());
                    }
                }

                commonModes = modes;
                modes.clear();
            }
        }
    }

    d->ui.changeMode->clear();
    d->ui.changeMode->addItems(commonModes);
    d->ui.changeMode->setDisabled(commonModes.isEmpty());

    // find the display mode to activate
    for (const auto& view : views) {
        if (auto* prop =
                dynamic_cast<App::PropertyEnumeration*>(view->getPropertyByName("DisplayMode"))) {
            QString activeMode = QString::fromLatin1(prop->getValueAsString());
            int index = d->ui.changeMode->findText(activeMode);
            if (index != -1) {
                d->ui.changeMode->setCurrentIndex(index);
                break;
            }
        }
    }
}

void DlgDisplayPropertiesImp::setColorPlot(const std::vector<Gui::ViewProvider*>& views)
{
    bool material = false;
    for (auto view : views) {
        auto* prop =
            dynamic_cast<App::PropertyAppearance*>(view->getPropertyByName("TextureMaterial"));
        if (prop) {
            material = true;
            break;
        }
    }

    d->ui.buttonColorPlot->setEnabled(material);
}

void DlgDisplayPropertiesImp::setShapeAppearance(const std::vector<Gui::ViewProvider*>& views)
{
    // Which look it is, and whether the list shows it, is setMaterialCard's
    // to say; this decides only whether there is one to edit at all.
    bool material = false;
    for (auto view : views) {
        if (auto* prop =
                dynamic_cast<App::PropertyAppearanceList*>(view->getPropertyByName("ShapeAppearance"))) {
            if (prop->getSize() == 0) {
                continue;
            }
            material = true;
            break;
        }
    }
    d->ui.buttonCustomAppearance->setEnabled(material);
}

namespace
{

/// The label a finish pattern wears in the preset list. Not the canonical
/// name App::SurfaceFinish uses -- that one is an API spelling ("knurl-
/// straight"), and this one is read by a person.
QString finishPresetLabel(uint8_t pattern)
{
    switch (pattern) {
        case App::SurfaceFinish::Knurl:
            return DlgDisplayPropertiesImp::tr("Diamond knurl");
        case App::SurfaceFinish::KnurlStraight:
            return DlgDisplayPropertiesImp::tr("Straight knurl");
        case App::SurfaceFinish::Brushed:
            return DlgDisplayPropertiesImp::tr("Brushed");
        case App::SurfaceFinish::Blasted:
            return DlgDisplayPropertiesImp::tr("Blasted");
        case App::SurfaceFinish::Turned:
            return DlgDisplayPropertiesImp::tr("Turned");
        default:
            return {};
    }
}

/// The pattern a label names, or None for anything this build does not
/// offer -- including the empty box, which is how None is written.
uint8_t finishPatternFromLabel(const QString& label)
{
    const QString wanted = label.trimmed();
    if (wanted.isEmpty()) {
        return App::SurfaceFinish::None;
    }
    for (uint8_t pattern = 1; pattern < App::SurfaceFinish::PatternCount; ++pattern) {
        if (finishPresetLabel(pattern).compare(wanted, Qt::CaseInsensitive) == 0) {
            return pattern;
        }
    }
    return App::SurfaceFinish::None;
}

}  // namespace

void DlgDisplayPropertiesImp::setupFinishPresets()
{
    // A line edit with the presets behind it rather than a combo box:
    // the list is short but the names are not guessable from an icon,
    // and typing three letters beats opening a drop-down and reading
    // six rows. The icons stay -- they are what the patterns are
    // recognised by -- at the size the material selection above uses,
    // so the two lists in this dialog look like one dialog.
    const int extent = MaterialTreeWidget::iconExtent();
    QPixmap blank(extent, extent);
    blank.fill(Qt::transparent);

    d->finishModel = new QStandardItemModel(this);
    auto addRow = [this, &blank](const QIcon& icon, const QString& label, uint8_t pattern) {
        auto row = new QStandardItem(icon.isNull() ? QIcon(blank) : icon, label);
        row->setData(uint(pattern), Qt::UserRole);
        d->finishModel->appendRow(row);
    };
    // The blank behind "None" is the same size as a pattern's icon, so
    // its label lines up with theirs instead of sliding into the icon
    // column.
    addRow(QIcon(blank), tr("None"), App::SurfaceFinish::None);
    for (uint8_t pattern = 1; pattern < App::SurfaceFinish::PatternCount; ++pattern) {
        // Each preset is drawn at the size that pattern has on a real
        // part, which is the one size MaterialIcons ships a rendered
        // icon for -- so the list paints from the resource rather than
        // waiting on the render queue.
        const App::SurfaceFinish preset = MaterialIcons::defaultFinish(pattern);
        addRow(MaterialIcons::instance().finishIcon(preset),
               finishPresetLabel(pattern), pattern);
    }

    d->finishCompleter = new QCompleter(d->finishModel, this);
    d->finishCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    // Contains, not starts-with: "knurl" is the word that tells the two
    // knurls apart, and it is not at the front of either label.
    d->finishCompleter->setFilterMode(Qt::MatchContains);
    d->finishCompleter->setCompletionMode(QCompleter::PopupCompletion);
    d->ui.editFinish->setCompleter(d->finishCompleter);
    d->finishCompleter->popup()->setIconSize(QSize(extent, extent));
    // Connected here rather than in setupConnections(), which runs
    // before this and would be handed a null completer.
    connect(d->finishCompleter,
            qOverload<const QString&>(&QCompleter::activated),
            this,
            &DlgDisplayPropertiesImp::onFinishPresetActivated);

    // Clicking the box offers the whole list, the way a combo box would.
    // Without this the control is a text field that happens to complete,
    // and a preset nobody can name is a preset nobody finds.
    d->ui.editFinish->installEventFilter(this);
}

void DlgDisplayPropertiesImp::inlineCompleteFinish(const QString& typed)
{
    if (typed.isEmpty()) {
        return;
    }
    d->finishCompleter->setCompletionPrefix(typed);
    const QString match = d->finishCompleter->currentCompletion();
    // Only when the match CONTINUES what was typed. The completer also
    // matches in the middle of a name -- "knurl" finds both knurls --
    // and there is no way to write the rest of a word in front of the
    // cursor.
    if (match.isEmpty() || !match.startsWith(typed, Qt::CaseInsensitive)) {
        return;
    }
    QSignalBlocker block(d->ui.editFinish);
    d->ui.editFinish->setText(match);
    // The part nobody typed stays selected, so the next keystroke
    // replaces it and the one after Enter accepts it.
    d->ui.editFinish->setSelection(int(typed.length()),
                                   int(match.length() - typed.length()));
}

bool DlgDisplayPropertiesImp::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == d->ui.editFinish && d->finishCompleter
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::FocusIn)) {
        // An empty prefix matches every row, so this is "show the list".
        d->finishCompleter->setCompletionPrefix(QString());
        d->finishCompleter->complete();
    }
    return QDialog::eventFilter(watched, event);
}

void DlgDisplayPropertiesImp::setShapeFinish(const std::vector<Gui::ViewProvider*>& views)
{
    bool hasAppearance = false;
    App::SurfaceFinish finish;
    for (auto view : views) {
        if (auto* prop = dynamic_cast<App::PropertyAppearanceList*>(
                view->getPropertyByName("ShapeAppearance"))) {
            if (prop->getSize() == 0) {
                continue;
            }
            hasAppearance = true;
            // Entry 0 is the finish the dialog edits: it writes every
            // entry alike, and a per-face finish authored from a script
            // still reads back as its first face rather than as nothing.
            finish = prop->getFinish(0);
            break;
        }
    }

    QSignalBlocker blockFinish(d->ui.editFinish);
    QSignalBlocker blockPitch(d->ui.spinFinishPitch);
    QSignalBlocker blockDepth(d->ui.spinFinishDepth);
    QSignalBlocker blockAngle(d->ui.spinFinishAngle);

    // A pattern this build cannot draw -- a document written by a later
    // one -- has no label here, and reads as unfinished rather than as a
    // silently wrong preset. The value itself survives untouched so long
    // as nobody picks one.
    const QString label = finishPresetLabel(finish.pattern);
    d->ui.editFinish->setText(label);
    // MinPitch is the stored spelling of "unstated": SurfaceFinish::
    // normalize() clamps a set pattern's pitch up to it, and the view
    // provider reads anything that low back as "give me this pattern's
    // own size". Show it as the automatic it is.
    const bool set = finish.isSet() && !label.isEmpty();
    d->ui.spinFinishPitch->setValue(
        set && finish.pitch > App::SurfaceFinish::MinPitch ? finish.pitch : 0.0);
    d->ui.spinFinishDepth->setValue(set ? finish.depth : 0.0);
    d->ui.spinFinishAngle->setValue(set ? finish.angle : 0.0);

    d->ui.editFinish->setEnabled(hasAppearance);
    d->ui.spinFinishPitch->setEnabled(hasAppearance && set);
    d->ui.spinFinishDepth->setEnabled(hasAppearance && set);
    d->ui.spinFinishAngle->setEnabled(hasAppearance && set);
}

void DlgDisplayPropertiesImp::applyFinish()
{
    App::SurfaceFinish finish;
    finish.pattern = finishPatternFromLabel(d->ui.editFinish->text());
    if (finish.isSet()) {
        // 0 = automatic in all three, which normalize() stores as the
        // MinPitch floor for the pitch and as a plain 0 for the rest.
        finish.pitch = float(d->ui.spinFinishPitch->value());
        finish.depth = float(d->ui.spinFinishDepth->value());
        finish.angle = float(d->ui.spinFinishAngle->value());
    }

    for (auto view : getTargets()) {
        if (auto* prop = dynamic_cast<App::PropertyAppearanceList*>(
                view->getPropertyByName("ShapeAppearance"))) {
            // The uniform setter: every entry gets this finish, and an
            // unset one clears the field entirely rather than storing a
            // row of zeros -- which is what lets hasFinish() stay false
            // and the legacy Render_Finish* knobs keep working.
            prop->setFinish(finish);
        }
    }
}

void DlgDisplayPropertiesImp::onFinishPresetActivated(const QString& label)
{
    // Typed rather than chosen, and not a preset: put back what the
    // objects actually carry instead of silently reading it as None.
    // The empty box IS None, so it is left alone.
    const uint8_t pattern = finishPatternFromLabel(label);
    if (pattern == App::SurfaceFinish::None && !label.trimmed().isEmpty()) {
        setShapeFinish(getTargets());
        return;
    }
    // Write the label back rather than trusting what is in the box: the
    // completer sets the text on activation too, and which of the two
    // runs first is connection order -- not something to leave applyFinish
    // depending on.
    {
        QSignalBlocker block(d->ui.editFinish);
        d->ui.editFinish->setText(finishPresetLabel(pattern));
        d->finishTyped = d->ui.editFinish->text();
    }
    {
        // A preset is a pattern at its own size, so picking one drops
        // any sizes the previous pattern was given -- they meant nothing
        // to this one. Customisation is what the three spin boxes are
        // for, after.
        QSignalBlocker blockPitch(d->ui.spinFinishPitch);
        QSignalBlocker blockDepth(d->ui.spinFinishDepth);
        QSignalBlocker blockAngle(d->ui.spinFinishAngle);
        d->ui.spinFinishPitch->setValue(0.0);
        d->ui.spinFinishDepth->setValue(0.0);
        d->ui.spinFinishAngle->setValue(0.0);
    }
    const bool set = pattern != App::SurfaceFinish::None;
    d->ui.spinFinishPitch->setEnabled(set);
    d->ui.spinFinishDepth->setEnabled(set);
    d->ui.spinFinishAngle->setEnabled(set);
    applyFinish();
}

void DlgDisplayPropertiesImp::onFinishSizeChanged(double)
{
    applyFinish();
}

void DlgDisplayPropertiesImp::setShapeColor(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setElementColor(views, "ShapeColor", d->ui.buttonColor);
}

void DlgDisplayPropertiesImp::setLineColor(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setElementColor(views, "LineColor", d->ui.buttonLineColor);
}

void DlgDisplayPropertiesImp::setMapFaceColor(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setPropertyBool(views, "MapFaceColor", d->ui.checkBoxMapFaceColor);
}

void DlgDisplayPropertiesImp::setMapEdgeColor(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setPropertyBool(views, "MapEdgeColor", d->ui.checkBoxMapLineColor);
}

void DlgDisplayPropertiesImp::setMapVertexColor(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setPropertyBool(views, "MapVertexColor", d->ui.checkBoxMapPointColor);
}

void DlgDisplayPropertiesImp::setMapTransparency(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setPropertyBool(views, "MapTransparency", d->ui.checkBoxMapTransparency);
}

void DlgDisplayPropertiesImp::setPointColor(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setElementColor(views, "PointColor", d->ui.buttonPointColor);
}

void DlgDisplayPropertiesImp::setPointSize(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setDrawStyle(views, "PointSize", d->ui.spinPointSize);
}

void DlgDisplayPropertiesImp::setLineWidth(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setDrawStyle(views, "LineWidth", d->ui.spinLineWidth);
}

void DlgDisplayPropertiesImp::setTransparency(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setTransparency(views, "Transparency", d->ui.spinTransparency, d->ui.horizontalSlider);
}

void DlgDisplayPropertiesImp::setLineTransparency(const std::vector<Gui::ViewProvider*>& views)
{
    Private::setTransparency(views,
                             "LineTransparency",
                             d->ui.spinLineTransparency,
                             d->ui.sliderLineTransparency);
}

void DlgDisplayPropertiesImp::rememberTargets(const std::vector<Gui::ViewProvider*>& views)
{
    d->targets.clear();
    for (auto view : views) {
        auto vpd = Base::freecad_dynamic_cast<const Gui::ViewProviderDocumentObject>(view);
        if (!vpd) {
            continue;
        }
        auto obj = vpd->getObject();
        if (obj && obj->getDocument()) {
            d->targets.emplace_back(obj->getDocument()->getName(), obj->getNameInDocument());
        }
    }
}

std::vector<Gui::ViewProvider*> DlgDisplayPropertiesImp::getTargets() const
{
    std::vector<Gui::ViewProvider*> views;

    for (const auto& target : d->targets) {
        auto doc = App::GetApplication().getDocument(target.first.c_str());
        if (!doc) {
            continue;  // the document was closed under us
        }
        auto obj = doc->getObject(target.second.c_str());
        if (!obj) {
            continue;  // the object was deleted under us
        }
        auto guiDoc = Gui::Application::Instance->getDocument(doc);
        if (!guiDoc) {
            continue;
        }
        if (auto view = guiDoc->getViewProvider(obj)) {
            views.push_back(view);
        }
    }

    return views;
}

void DlgDisplayPropertiesImp::slotDeletedObject(const Gui::ViewProvider& obj)
{
    auto vpd = Base::freecad_dynamic_cast<const Gui::ViewProviderDocumentObject>(&obj);
    if (!vpd) {
        return;
    }
    auto object = vpd->getObject();
    if (!object || !object->getDocument()) {
        return;
    }

    // getTargets() would drop this one anyway once the object is gone, but the
    // signal arrives while it still resolves, so remove it by name here and let
    // the controls disable themselves if nothing is left.
    auto name = std::make_pair(std::string(object->getDocument()->getName()),
                               std::string(object->getNameInDocument()));
    auto it = std::find(d->targets.begin(), d->targets.end(), name);
    if (it != d->targets.end()) {
        d->targets.erase(it);
        setPropertiesFromSelection();
    }
}

void DlgDisplayPropertiesImp::slotDeleteDocument(const Gui::Document& doc)
{
    auto document = doc.getDocument();
    if (!document) {
        return;
    }

    std::string name = document->getName();
    auto it = std::remove_if(d->targets.begin(),
                             d->targets.end(),
                             [&name](const std::pair<std::string, std::string>& target) {
                                 return target.first == name;
                             });
    if (it != d->targets.end()) {
        d->targets.erase(it, d->targets.end());
        setPropertiesFromSelection();
    }
}

std::vector<Gui::ViewProvider*> DlgDisplayPropertiesImp::getSelection() const
{
    std::vector<Gui::ViewProvider*> views;

    // get the complete selection
    std::vector<Gui::SelectionSingleton::SelObj> sel = Gui::Selection().getCompleteSelection();
    for (const auto& it : sel) {
        Gui::ViewProvider* view =
            Gui::Application::Instance->getDocument(it.pDoc)->getViewProvider(it.pObject);
        views.push_back(view);
    }

    return views;
}

namespace
{

/// The object's material card property, or null. The card is on the OBJECT
/// (mass, FEM and CAM read it); the look is on the view provider.
Materials::PropertyMaterial* cardOf(Gui::ViewProvider* view)
{
    auto* vp = dynamic_cast<Gui::ViewProviderDocumentObject*>(view);
    if (!vp || !vp->getObject()) {
        return nullptr;
    }
    return dynamic_cast<Materials::PropertyMaterial*>(
            vp->getObject()->getPropertyByName("ShapeMaterial"));
}

}  // namespace

void DlgDisplayPropertiesImp::setMaterialCard(const std::vector<Gui::ViewProvider*>& views)
{
    // The picker is there only while the selection carries a card: without
    // one there is nothing for the look to follow, and the panel is what it
    // always was (docs/MaterialStorage.md 15.5).
    Materials::PropertyMaterial* card = nullptr;
    App::PropertyAppearanceList* appearance = nullptr;
    for (auto view : views) {
        if (!card) {
            card = cardOf(view);
        }
        if (!appearance) {
            appearance = dynamic_cast<App::PropertyAppearanceList*>(
                    view->getPropertyByName("ShapeAppearance"));
        }
    }
    d->ui.groupBoxCard->setVisible(card != nullptr);
    QString cardName;
    if (card) {
        try {
            const auto& material = card->getValue();
            cardName = material.getName();
            d->ui.widgetCard->setMaterial(material.getUUID());
        }
        catch (const Materials::MaterialNotFound&) {
        }
    }

    // "As material Steel", "Custom", "Custom, 3 faces painted"
    QString state;
    if (!appearance) {
        state = tr("Custom");
    }
    else if (appearance->isFollowingMaterial()) {
        state = cardName.isEmpty() ? tr("As material") : tr("As material %1").arg(cardName);
    }
    else {
        const std::size_t painted = appearance->getOverrides().size();
        state = painted ? tr("Custom, %n face(s) painted", "", int(painted)) : tr("Custom");
    }
    d->ui.labelAppearance->setText(state);
    // The list's selection says what the status line says: the top row
    // while the look is the card's, the look's own card once it is not.
    if (!appearance) {
        d->ui.widgetMaterial->setMaterial(QString());
    }
    else if (appearance->isFollowingMaterial()) {
        d->ui.widgetMaterial->setMaterial(asMaterialId());
    }
    else {
        d->ui.widgetMaterial->setMaterial(QString::fromStdString(appearance->getBase().uuid));
    }
    // Shown only while it applies, which is the rule the sync commands
    // follow (docs/MaterialStorage.md 13.5)
    d->ui.buttonResetToMaterial->setVisible(
            card != nullptr && appearance != nullptr && !appearance->isFollowingMaterial());

    // The same rule for the shader graph: the button is there while the
    // card carries a graph to put on the object, and reads Revert while
    // one is on it (17.11)
    bool hasGraph = false;
    App::ShaderBinding* materialized = nullptr;
    for (auto view : views) {
        if (auto* vp = dynamic_cast<Gui::ViewProviderDocumentObject*>(view)) {
            if (!materialized) {
                materialized = Materials::ShaderGraph::materialized(vp->getObject());
            }
        }
        if (auto* worn = cardOf(view)) {
            try {
                hasGraph = hasGraph || worn->getValue().hasMaterialX();
            }
            catch (const Materials::MaterialNotFound&) {
            }
        }
    }
    d->ui.buttonEditShaderGraph->setVisible(hasGraph || materialized);
    d->ui.buttonEditShaderGraph->setText(materialized ? tr("Revert Shader Graph")
                                                      : tr("Edit Shader Graph..."));
}

void DlgDisplayPropertiesImp::onEditShaderGraph()
{
    // Through Python, as the sync commands go: one primitive, undoable,
    // and on the macro record
    for (auto view : getTargets()) {
        auto* vp = dynamic_cast<Gui::ViewProviderDocumentObject*>(view);
        auto* obj = vp ? vp->getObject() : nullptr;
        if (!obj || !obj->getDocument()) {
            continue;
        }
        const char* doc = obj->getDocument()->getName();
        const char* name = obj->getNameInDocument();
        if (Materials::ShaderGraph::materialized(obj)) {
            if (Materials::ShaderGraph::edited(obj)) {
                auto answer = QMessageBox::question(
                    this,
                    tr("Revert Shader Graph"),
                    tr("The shader graph of %1 has been edited. Reverting discards the edits "
                       "and draws the card's graph again.")
                        .arg(QString::fromUtf8(obj->Label.getValue())),
                    QMessageBox::Discard | QMessageBox::Cancel,
                    QMessageBox::Cancel);
                if (answer != QMessageBox::Discard) {
                    continue;
                }
            }
            Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Revert shader graph"));
            Gui::Command::doCommand(Gui::Command::Doc,
                                    "import Materials\n"
                                    "Materials.revertShaderGraph(App.getDocument('%s').getObject('%s'))",
                                    doc,
                                    name);
            Gui::Command::commitCommand();
        }
        else if (cardOf(view)) {
            Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Edit shader graph"));
            Gui::Command::doCommand(Gui::Command::Doc,
                                    "import Materials\n"
                                    "Materials.materializeShaderGraph("
                                    "App.getDocument('%s').getObject('%s'), 'ShapeMaterial')",
                                    doc,
                                    name);
            Gui::Command::commitCommand();
        }
    }
    setMaterialCard(getTargets());
}

void DlgDisplayPropertiesImp::onCardSelected(const std::shared_ptr<Materials::Material>& material)
{
    for (auto view : getTargets()) {
        if (auto* card = cardOf(view)) {
            card->setValue(*material);
        }
    }
    setMaterialCard(getTargets());
}

void DlgDisplayPropertiesImp::onResetToMaterial()
{
    // One answer in one place: the context-menu command reaches the same
    // primitive (docs/MaterialStorage.md 15.5).
    for (auto view : getTargets()) {
        if (auto* vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(view)) {
            vp->resetAppearanceToMaterial();
        }
    }
    setMaterialCard(getTargets());
}

void DlgDisplayPropertiesImp::onMaterialSelected(
    const std::shared_ptr<Materials::Material>& material)
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    for (auto it : Provider) {
        if (auto* prop = dynamic_cast<App::PropertyAppearanceList*>(
                it->getPropertyByName("ShapeAppearance"))) {
            // A card states colours and gloss, never a machining, so the
            // material it builds carries finish None -- and this write
            // states every field of the base, which would take the finish
            // set one row down with it. Carry it across, and the texture
            // beside it, for the same reason applyWholeMaterial does.
            App::MaterialAppearance appearance = material->getMaterialAppearance();
            appearance.finish = prop->getBase().finish;
            appearance.texture = prop->getBase().texture;
            // The BASE: a look chosen here outranks the object's card from
            // now on (setBase ends the follow), and the faces holding a
            // look of their own keep it (docs/MaterialStorage.md 15.5).
            prop->setBase(appearance);
        }
        // A card may also state render features App::MaterialAppearance cannot
        // carry -- glass so far. Those are dynamic properties on the view
        // provider, so they are applied beside the appearance, not
        // through it. Applied unconditionally: a card that states none
        // clears what a previous card left.
        if (auto* vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(it)) {
            Gui::applyMaterialRenderProperties(vp, material->getRenderProperties());
        }
    }
    setMaterialCard(getTargets());
}

// ----------------------------------------------------------------------------

/* TRANSLATOR Gui::Dialog::TaskDisplayProperties */

TaskDisplayProperties::TaskDisplayProperties()
{
    this->setButtonPosition(TaskDisplayProperties::North);
    widget = new DlgDisplayPropertiesImp();
    addTaskBox(widget);
}

TaskDisplayProperties::~TaskDisplayProperties() = default;

QDialogButtonBox::StandardButtons TaskDisplayProperties::getStandardButtons() const
{
    return QDialogButtonBox::Close;
}

bool TaskDisplayProperties::reject()
{
    widget->reject();
    return (widget->result() == QDialog::Rejected);
}

#include "moc_DlgDisplayPropertiesImp.cpp"
