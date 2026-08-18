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

#include <QSignalBlocker>
#include <algorithm>
#include <fastsignals/signal.h>

#include <Base/Console.h>
#include <App/Application.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/DlgMaterialPropertiesImp.h>
#include <Gui/DockWindowManager.h>
#include <Gui/PrefWidgets.h>
#include <Gui/Document.h>
#include <Gui/Selection/Selection.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Gui/ViewProviderGeometryObject.h>
#include <Gui/WaitCursor.h>

#include <Mod/Material/App/ModelUuids.h>

#include "DlgDisplayPropertiesImp.h"
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
                    dynamic_cast<App::PropertyMaterial*>(view->getPropertyByName(property))) {
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

    {
        QSignalBlocker block(d->ui.widgetMaterial);
        rememberTargets(getSelection());
        setPropertiesFromSelection();
    }

    Gui::Selection().Attach(this);

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
    Gui::Selection().Detach(this);
}

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

    filter = std::make_shared<Materials::MaterialFilter>();
    filter->setName(tr("All materials"));
    filterList->push_back(filter);

    d->ui.widgetMaterial->setIncludeEmptyFolders(false);
    d->ui.widgetMaterial->setIncludeLegacy(false);

    d->ui.widgetMaterial->setFilter(filterList);
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
    setShapeAppearance(views);
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
        else if (prop.isDerivedFrom<App::PropertyMaterialList>()) {
            if (prop_name == "ShapeAppearance") {
                // No getValues() on this fork's PropertyMaterialList by design
                // -- read the one entry, which returns by value.
                const auto& matList = static_cast<const App::PropertyMaterialList&>(prop);
                if (matList.getSize() > 0) {
                    App::Material material = matList.getMaterial(0);
                    d->ui.widgetMaterial->setMaterial(QString::fromStdString(material.uuid));
                }
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
            dynamic_cast<App::PropertyMaterial*>(view->getPropertyByName("TextureMaterial"));
        if (prop) {
            material = true;
            break;
        }
    }

    d->ui.buttonColorPlot->setEnabled(material);
}

void DlgDisplayPropertiesImp::setShapeAppearance(const std::vector<Gui::ViewProvider*>& views)
{
    bool material = false;
    App::Material mat = App::Material(App::Material::DEFAULT);
    for (auto view : views) {
        if (auto* prop =
                dynamic_cast<App::PropertyMaterialList*>(view->getPropertyByName("ShapeAppearance"))) {
            if (prop->getSize() == 0) {
                continue;
            }
            material = true;
            mat = prop->getMaterial(0);
            d->ui.widgetMaterial->setMaterial(QString::fromStdString(mat.uuid));
            break;
        }
    }
    d->ui.buttonCustomAppearance->setEnabled(material);
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

void DlgDisplayPropertiesImp::onMaterialSelected(
    const std::shared_ptr<Materials::Material>& material)
{
    std::vector<Gui::ViewProvider*> Provider = getTargets();
    for (auto it : Provider) {
        if (auto* prop = dynamic_cast<App::PropertyMaterialList*>(
                it->getPropertyByName("ShapeAppearance"))) {
            prop->setValue(material->getMaterialAppearance());
        }
    }
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
