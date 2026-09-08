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

#include <QDockWidget>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QString>
#include <algorithm>
#include <fastsignals/signal.h>

#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/DockWindowManager.h>
#include <Gui/Document.h>
#include <Gui/Selection/Selection.h>
#include <Gui/ViewProvider.h>
#include <Gui/ViewProviderGeometryObject.h>
#include <Gui/WaitCursor.h>

#include <Mod/Material/App/Exceptions.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/ModelUuids.h>
#include <Mod/Material/App/PropertyMaterial.h>

#include "DlgMaterialImp.h"
#include "MaterialIcons.h"
#include "MaterialTreeWidget.h"
#include "ui_DlgMaterial.h"


using namespace MatGui;
using namespace std;
namespace sp = std::placeholders;


/* TRANSLATOR Gui::Dialog::DlgMaterialImp */

#if 0  // needed for Qt's lupdate utility
    qApp->translate("QDockWidget", "Material");
#endif

class DlgMaterialImp::Private
{
    using DlgMaterialImp_Connection = fastsignals::connection;

public:
    Ui::DlgMaterial ui;
    bool floating;
    DlgMaterialImp_Connection connectChangedObject;
};

/**
 *  Constructs a DlgMaterialImp which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgMaterialImp::DlgMaterialImp(bool floating, QWidget* parent, Qt::WindowFlags fl)
    : QDialog(parent, fl)
    , d(new Private)
{
    d->ui.setupUi(this);
    setupConnections();

    d->floating = floating;

    // Create a filter to only include current format materials
    // that contain physical properties.
    Materials::MaterialFilter filter;
    filter.requirePhysical(true);
    d->ui.widgetMaterial->setFilter(filter);

    std::vector<App::DocumentObject*> objects = getSelectionObjects();
    setMaterial(objects);

    // embed this dialog into a dockable widget container
    if (floating) {
        Gui::DockWindowManager* pDockMgr = Gui::DockWindowManager::instance();
        QDockWidget* dw =
            pDockMgr->addDockWindow("Display Properties", this, Qt::AllDockWidgetAreas);
        dw->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        dw->setFloating(true);
        dw->show();
    }

    Gui::SelectionRoom().Attach(this);

    // NOLINTBEGIN
    d->connectChangedObject = Gui::Application::Instance->signalChangedObject.connect(
        std::bind(&DlgMaterialImp::slotChangedObject, this, sp::_1, sp::_2));
    // NOLINTEND
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgMaterialImp::~DlgMaterialImp()
{
    // no need to delete child widgets, Qt does it all for us
    d->connectChangedObject.disconnect();
    Gui::SelectionRoom().Detach(this);
}

void DlgMaterialImp::setupConnections()
{
    connect(d->ui.widgetMaterial,
            &MaterialTreeWidget::materialSelected,
            this,
            &DlgMaterialImp::onMaterialSelected);
    connect(d->ui.buttonResetToMaterial,
            &QPushButton::clicked,
            this,
            &DlgMaterialImp::onResetToMaterial);
    // A rendered icon arrives after the render that makes it, so ask again
    // when one lands rather than showing the blank it answered with first.
    connect(&MaterialIcons::instance(), &MaterialIcons::iconReady, this, [this]() {
        setAssignNote(getSelectionObjects());
    });
}

void DlgMaterialImp::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        d->ui.retranslateUi(this);
    }
    QDialog::changeEvent(e);
}

/// @cond DOXERR
void DlgMaterialImp::OnChange(Gui::SelectionSingleton::SubjectType& rCaller,
                              Gui::SelectionSingleton::MessageType Reason)
{
    Q_UNUSED(rCaller);
    if (Reason.Type == Gui::SelectionChanges::AddSelection
        || Reason.Type == Gui::SelectionChanges::RmvSelection
        || Reason.Type == Gui::SelectionChanges::SetSelection
        || Reason.Type == Gui::SelectionChanges::ClrSelection) {
        std::vector<App::DocumentObject*> objects = getSelectionObjects();
        setMaterial(objects);
    }
}
/// @endcond

void DlgMaterialImp::slotChangedObject(const Gui::ViewProvider& obj, const App::Property& prop)
{
    // This method gets called if a property of any view provider is changed.
    // We pick out all the properties for which we need to update this dialog.
    std::vector<Gui::ViewProvider*> Provider = getSelection();
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
        if (prop.isDerivedFrom<App::PropertyAppearance>()) {
            //auto& value = static_cast<const App::PropertyAppearance&>(prop).getValue();
            if (prop_name == "ShapeMaterial") {
                // bool blocked = d->ui.buttonColor->blockSignals(true);
                // auto color = value.diffuseColor;
                // d->ui.buttonColor->setColor(QColor((int)(255.0f * color.r),
                //                                    (int)(255.0f * color.g),
                //                                    (int)(255.0f * color.b)));
                // d->ui.buttonColor->blockSignals(blocked);
            }
        }
        else if (prop.isDerivedFrom<App::PropertyAppearanceList>()
                 && prop_name == "ShapeAppearance") {
            // What assigning would do depends on whether the look still
            // follows the card, and that answer changes the moment someone
            // sets a look by hand -- with this panel open in front of them.
            setAssignNote(getSelectionObjects());
        }
    }
}

/**
 * Destroys the dock window this object is embedded into without destroying itself.
 */
void DlgMaterialImp::reject()
{
    if (d->floating) {
        // closes the dock window
        Gui::DockWindowManager* pDockMgr = Gui::DockWindowManager::instance();
        pDockMgr->removeDockWindow(this);
    }
    QDialog::reject();
}

void DlgMaterialImp::setMaterial(const std::vector<App::DocumentObject*>& objects)
{
    for (auto it : objects) {
        if (auto prop = dynamic_cast<Materials::PropertyMaterial*>(it->getPropertyByName("ShapeMaterial"))) {
            try {
                const auto& material = prop->getValue();
                d->ui.widgetMaterial->setMaterial(material.getUUID());
                setAssignNote(objects);
                return;
            }
            catch (const Materials::MaterialNotFound&) {
            }
        }
    }
    d->ui.widgetMaterial->setMaterial(Materials::MaterialManager::defaultMaterialUUID());
    setAssignNote(objects);
}

void DlgMaterialImp::setAssignNote(const std::vector<App::DocumentObject*>& objects)
{
    // The card the picker is showing, which is the one an assignment would
    // apply -- not necessarily the one the objects carry.
    QIcon look;
    const QString uuid = d->ui.widgetMaterial->getMaterialUUID();
    if (!uuid.isEmpty()) {
        try {
            auto card = Materials::MaterialManager::getManager().getMaterial(uuid);
            look = MaterialIcons::instance().icon(uuid,
                                                  card->getMaterialAppearance(),
                                                  card->getName(),
                                                  card->getRenderProperties());
        }
        catch (const Materials::MaterialNotFound&) {
        }
    }
    const int extent = MaterialTreeWidget::iconExtent();
    d->ui.labelPreview->setPixmap(look.isNull() ? QPixmap() : look.pixmap(extent, extent));
    d->ui.labelPreview->setVisible(!look.isNull());

    // Whether assigning would carry the look across is the follow flag's
    // answer, and it is per object: one object still following is enough
    // for the assignment to change how something looks.
    bool anyFollows = false;
    bool anyCustom = false;
    for (auto object : objects) {
        auto* vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(
            Gui::Application::Instance->getViewProvider(object));
        if (!vp) {
            continue;
        }
        if (vp->ShapeAppearance.isFollowingMaterial()) {
            anyFollows = true;
        }
        else if (vp->canResetAppearanceToMaterial()) {
            anyCustom = true;
        }
    }
    if (anyFollows) {
        d->ui.labelAssign->setText(tr("Assigning also applies this card's appearance."));
    }
    else if (anyCustom) {
        d->ui.labelAssign->setText(tr("The appearance was set by hand and is kept as it is."));
    }
    else {
        d->ui.labelAssign->setText(QString());
    }
    // Offered only while it applies, the rule the appearance panel and the
    // context-menu command both follow (docs/MaterialStorage.md 13.5, 15.5)
    d->ui.buttonResetToMaterial->setVisible(anyCustom);
}

void DlgMaterialImp::onResetToMaterial()
{
    for (auto object : getSelectionObjects()) {
        auto* vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(
            Gui::Application::Instance->getViewProvider(object));
        if (vp) {
            vp->resetAppearanceToMaterial();
        }
    }
    setAssignNote(getSelectionObjects());
}

std::vector<Gui::ViewProvider*> DlgMaterialImp::getSelection() const
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

std::vector<App::DocumentObject*> DlgMaterialImp::getSelectionObjects() const
{
    std::vector<App::DocumentObject*> objects;

    // get the complete selection
    std::vector<Gui::SelectionSingleton::SelObj> sel = Gui::Selection().getCompleteSelection();
    for (const auto& it : sel) {
        objects.push_back(it.pObject);
    }

    return objects;
}

void DlgMaterialImp::onMaterialSelected(const std::shared_ptr<Materials::Material>& material)
{
    std::vector<App::DocumentObject*> objects = getSelectionObjects();
    for (auto it : objects) {
        if (auto prop = dynamic_cast<Materials::PropertyMaterial*>(it->getPropertyByName("ShapeMaterial"))) {
            prop->setValue(*material);
        }
    }
    setAssignNote(objects);
}

// ----------------------------------------------------------------------------

/* TRANSLATOR Gui::Dialog::TaskMaterial */

TaskMaterial::TaskMaterial()
{
    this->setButtonPosition(TaskMaterial::North);
    widget = new DlgMaterialImp(false);
    taskbox = new Gui::TaskView::TaskBox(QPixmap(), widget->windowTitle(), true, nullptr);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);

    // This fork's Command API has no transaction ids: openCommand names the
    // active transaction, and commit/abort close that one.
    Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Set Material"));
}

TaskMaterial::~TaskMaterial() = default;

QDialogButtonBox::StandardButtons TaskMaterial::getStandardButtons() const
{
    return QDialogButtonBox::Ok | QDialogButtonBox::Cancel;
}

bool TaskMaterial::accept()
{
    Gui::Command::commitCommand();
    return true;
}

bool TaskMaterial::reject()
{
    Gui::Command::abortCommand();
    widget->reject();
    return (widget->result() == QDialog::Rejected);
}

#include "moc_DlgMaterialImp.cpp"
