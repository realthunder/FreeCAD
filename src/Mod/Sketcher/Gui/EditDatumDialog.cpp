/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
/// Qt Include Files
#include <Inventor/sensors/SoSensor.h>
#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QTimer>
#endif

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Expression.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Quantity.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/BitmapFactory.h>
#include <Gui/DatumValueEditor.h>
#include <Gui/SoDatumLabel.h>
#include <Gui/MainWindow.h>
#include <Gui/Notifications.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewerContext.h>
#include <Mod/Sketcher/App/GeometryFacade.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/OriginFeature.h>
#include <Precision.hxx>
#include <Inventor/SbViewportRegion.h>
#include <algorithm>
#include <cmath>

#include "CommandSketcherTools.h"
#include "EditDatumDialog.h"
#include "SketcherSettings.h"
#include "Utils.h"
#include "ViewProviderSketch.h"
#include "ui_InsertDatum.h"


using namespace SketcherGui;

/* TRANSLATOR SketcherGui::EditDatumDialog */

namespace
{
ViewProviderSketch* editedViewProvider(Sketcher::SketchObject* sketch);
bool isWidgetless(const Gui::ViewerContext* viewer);
}  // namespace

bool SketcherGui::checkConstraintName(const Sketcher::SketchObject* sketch,
                                      const std::string& constraintName)
{
    if (!constraintName.empty() && constraintName != Base::Tools::getIdentifier(constraintName)) {
        Gui::NotifyUserError(
            sketch,
            QT_TRANSLATE_NOOP("Notifications", "Value Error"),
            QT_TRANSLATE_NOOP("Notifications",
                              "Invalid constraint name (must only contain alphanumericals and "
                              "underscores, and must not start with digit)"));
        return false;
    }

    return true;
}

EditDatumDialog::EditDatumDialog(ViewProviderSketch* vp, int ConstrNbr)
    : ConstrNbr(ConstrNbr)
    , success(false)
{
    sketch = vp->getSketchObject();
    const std::vector<Sketcher::Constraint*>& Constraints = sketch->Constraints.getValues();
    Constr = Constraints[ConstrNbr];
}

EditDatumDialog::EditDatumDialog(Sketcher::SketchObject* pcSketch, int ConstrNbr)
    : sketch(pcSketch)
    , ConstrNbr(ConstrNbr)
    , success(false)
{
    const std::vector<Sketcher::Constraint*>& Constraints = sketch->Constraints.getValues();
    Constr = Constraints[ConstrNbr];
}

EditDatumDialog::~EditDatumDialog()
{}

int EditDatumDialog::exec(bool atCursor)
{
    // Not for a view nobody sits at, whoever asks: a modal dialog on the
    // host stops every client it serves (editDatums() is the way in).
    if (ViewProviderSketch* vp = editedViewProvider(sketch)) {
        if (isWidgetless(vp->getEditViewer())) {
            Base::Console().Warning("Sketcher: no datum dialog for a served view\n");
            Gui::Command::abortCommand();
            return QDialog::Rejected;
        }
    }

    // Return if constraint doesn't have editable value
    if (Constr->isDimensional()) {

        if (sketch->hasConflicts()) {
            Gui::TranslatedUserWarning(sketch,
                                       QObject::tr("Dimensional constraint"),
                                       QObject::tr("Not allowed to edit the datum because the "
                                                   "sketch contains conflicting constraints"));
            return QDialog::Rejected;
        }

        Base::Quantity init_val;

        QDialog dlg(Gui::getMainWindow());
        if (!ui_ins_datum) {
            ui_ins_datum.reset(new Ui_InsertDatum);
            ui_ins_datum->setupUi(&dlg);
        }
        double datum = Constr->getValue();

        ui_ins_datum->labelEdit->setEntryName(QByteArray("DatumValue"));
        if (Constr->Type == Sketcher::Angle) {
            datum = Base::toDegrees<double>(datum);
            dlg.setWindowTitle(tr("Insert Angle"));
            init_val.setUnit(Base::Unit::Angle);
            ui_ins_datum->label->setText(tr("Angle:"));
            ui_ins_datum->labelEdit->setParamGrpPath(
                QByteArray("User parameter:BaseApp/History/SketcherAngle"));
        }
        else if (Constr->Type == Sketcher::Radius) {
            dlg.setWindowTitle(tr("Insert Radius"));
            init_val.setUnit(Base::Unit::Length);
            ui_ins_datum->label->setText(tr("Radius:"));
            ui_ins_datum->labelEdit->setParamGrpPath(
                QByteArray("User parameter:BaseApp/History/SketcherLength"));
        }
        else if (Constr->Type == Sketcher::Diameter) {
            dlg.setWindowTitle(tr("Insert Diameter"));
            init_val.setUnit(Base::Unit::Length);
            ui_ins_datum->label->setText(tr("Diameter:"));
            ui_ins_datum->labelEdit->setParamGrpPath(
                QByteArray("User parameter:BaseApp/History/SketcherLength"));
        }
        else if (Constr->Type == Sketcher::Weight) {
            dlg.setWindowTitle(tr("Insert Weight"));
            ui_ins_datum->label->setText(tr("Weight:"));
            ui_ins_datum->labelEdit->setParamGrpPath(
                QByteArray("User parameter:BaseApp/History/SketcherWeight"));
        }
        else if (Constr->Type == Sketcher::SnellsLaw) {
            dlg.setWindowTitle(tr("Refractive Index Ratio", "Constraint_SnellsLaw"));
            ui_ins_datum->label->setText(tr("Ratio n2/n1:", "Constraint_SnellsLaw"));
            ui_ins_datum->labelEdit->setParamGrpPath(
                QByteArray("User parameter:BaseApp/History/SketcherRefrIndexRatio"));
            ui_ins_datum->labelEdit->setSingleStep(0.05);
        }
        else {
            dlg.setWindowTitle(tr("Insert Length"));
            init_val.setUnit(Base::Unit::Length);
            ui_ins_datum->label->setText(tr("Length:"));
            ui_ins_datum->labelEdit->setParamGrpPath(
                QByteArray("User parameter:BaseApp/History/SketcherLength"));
        }

        init_val.setValue(datum);

        ui_ins_datum->labelEdit->setValue(init_val);
        ui_ins_datum->labelEdit->pushToHistory();
        ui_ins_datum->labelEdit->selectNumber();
        ui_ins_datum->labelEdit->bind(sketch->Constraints.createPath(ConstrNbr));
        ui_ins_datum->name->setText(Base::Tools::fromStdString(Constr->Name));

        ui_ins_datum->cbDriving->setChecked(!Constr->isDriving);

        connect(ui_ins_datum->cbDriving,
                &QCheckBox::toggled,
                this,
                &EditDatumDialog::drivingToggled);
        connect(ui_ins_datum->labelEdit,
                qOverload<const Base::Quantity&>(&Gui::QuantitySpinBox::valueChanged),
                this,
                &EditDatumDialog::datumChanged);
        connect(ui_ins_datum->labelEdit,
                &Gui::QuantitySpinBox::showFormulaDialog,
                this,
                &EditDatumDialog::formEditorOpened);
        connect(&dlg, &QDialog::accepted, this, &EditDatumDialog::accepted);
        connect(&dlg, &QDialog::rejected, this, &EditDatumDialog::rejected);

        if (atCursor) {
            dlg.show();  // Need to show the dialog so geometry is computed
            QRect pg = dlg.parentWidget()->geometry();
            int Xmin = pg.x() + 10;
            int Ymin = pg.y() + 10;
            int Xmax = pg.x() + pg.width() - dlg.geometry().width() - 10;
            int Ymax = pg.y() + pg.height() - dlg.geometry().height() - 10;
            int x = Xmax < Xmin ? (Xmin + Xmax) / 2
                                : std::min(std::max(QCursor::pos().x(), Xmin), Xmax);
            int y = Ymax < Ymin ? (Ymin + Ymax) / 2
                                : std::min(std::max(QCursor::pos().y(), Ymin), Ymax);
            dlg.setGeometry(x, y, dlg.geometry().width(), dlg.geometry().height());
        }

        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Mod/Sketcher");
        int width = hGrp->GetInt("DatumDialogWidth",0);
        if (width > 100)
            dlg.resize(width, dlg.height());
        auto res = dlg.exec();
        hGrp->SetInt("DatumDialogWidth", dlg.width());
        return res;
    }

    return QDialog::Rejected;
}

void EditDatumDialog::accepted()
{
    Base::Quantity newQuant = ui_ins_datum->labelEdit->value();
    if (newQuant.isQuantity() || (Constr->Type == Sketcher::SnellsLaw && newQuant.isDimensionless())
        || (Constr->Type == Sketcher::Weight && newQuant.isDimensionless())) {

        // save the value for the history
        ui_ins_datum->labelEdit->pushToHistory();

        double newDatum = newQuant.getValue();

        App::AutoTransaction transaction("Edit sketch datum");

        try {

            /*if (ui_ins_datum->cbDriving->isChecked() == Constr->isDriving) {
                Gui::cmdAppObjectArgs(sketch, "toggleDriving(%i)", ConstrNbr);
            }*/

            if (!ui_ins_datum->cbDriving->isChecked()) {
                if (ui_ins_datum->labelEdit->hasExpression()) {
                    ui_ins_datum->labelEdit->apply();
                }
                else {
                    performAutoScale(newDatum);

                    Gui::cmdAppObjectArgs(sketch,
                                          "setDatum(%i,App.Units.Quantity('%.15g %s'))",
                                          ConstrNbr,
                                          newDatum,
                                          Base::Tools::escapeEncodeString(
                                              QString::fromStdString(newQuant.getUnit().getString()))
                                              .toUtf8()
                                              .constData());
                }
            }

            std::string constraintName = ui_ins_datum->name->text().trimmed().toStdString();
            if (constraintName != sketch->Constraints[ConstrNbr]->Name
                && SketcherGui::checkConstraintName(sketch, constraintName)) {
                Gui::cmdAppObjectArgs(sketch,
                                      "renameConstraint(%d, u'%s')",
                                      ConstrNbr,
                                      constraintName.c_str());
            }

            Gui::Command::commitCommand();

            if (sketch->noRecomputes && sketch->ExpressionEngine.depsAreTouched()) {
                sketch->ExpressionEngine.execute();
                sketch->solve();
            }

            tryAutoRecompute(sketch);
            success = true;
        }
        catch (const Base::Exception& e) {
            Gui::NotifyUserError(sketch,
                                 QT_TRANSLATE_NOOP("Notifications", "Value Error"),
                                 e.what());

            Gui::Command::abortCommand();

            if (sketch->noRecomputes) {  // if setdatum failed, it is highly likely that solver
                                         // information is invalid.
                sketch->solve();
            }
        }
    }
}

void EditDatumDialog::rejected()
{
    Gui::Command::abortCommand();
    sketch->recomputeFeature();
}

bool EditDatumDialog::isSuccess()
{
    return success;
}

void EditDatumDialog::drivingToggled(bool state)
{
    if (state) {
        ui_ins_datum->labelEdit->setToLastUsedValue();
    }
    sketch->setDriving(ConstrNbr, !state);
    if (!sketch->noRecomputes) {  // if noRecomputes, solve() is already done by setDriving()
        sketch->solve();
    }
}

void EditDatumDialog::datumChanged()
{
    if (ui_ins_datum->labelEdit->text() != std::as_const(ui_ins_datum->labelEdit)->getHistory()[0]) {
        ui_ins_datum->cbDriving->setChecked(false);
    }
}

void EditDatumDialog::formEditorOpened(bool state)
{
    if (state) {
        ui_ins_datum->cbDriving->setChecked(false);
    }
}

// Whether obj's visible flag, and that of every parent up to lastParent
// (assumed visible when given), is up in doc
static bool isVisibleUpTo(App::DocumentObject* obj, Gui::Document* doc, App::DocumentObject* lastParent)
{
    while (obj && obj != lastParent) {
        auto parentviewprovider = doc->getViewProvider(obj);

        if (!parentviewprovider || !parentviewprovider->isVisible()) {
            return false;
        }
        obj = obj->getFirstParent();
    }
    return true;
}

// Whether something other than obj is shown in doc that gives a sense of
// scale: a visible geometric feature with a bounding box, looked for
// through links into other documents as well
static bool hasVisualFeature(App::DocumentObject* obj, App::DocumentObject* rootObj, Gui::Document* doc)
{
    auto docObjects = doc->getDocument()->getObjects();
    for (auto object : docObjects) {

        // Presumably, the sketch that is being edited has visual features, but
        // that's not interesting
        if (object == obj) {
            continue;
        }

        // No need to continue analysis if the object's visible flag is down
        bool visible = isVisibleUpTo(object, doc, rootObj);
        if (!visible) {
            continue;
        }

        App::DocumentObject* link = object->getLinkedObject();
        if (link && link->getDocument() != doc->getDocument()) {
            Gui::Document* linkDoc = Gui::Application::Instance->getDocument(link->getDocument());
            if (linkDoc && hasVisualFeature(link, link, linkDoc)) {
                return true;
            }
            continue;
        }

        // Skip objects that are not of geometric nature
        if (!object->isDerivedFrom<App::GeoFeature>()) {
            continue;
        }

        // Skip datum objects
        if (object->isDerivedFrom<App::OriginFeature>()) {
            continue;
        }

        // Skip container objects because getting their bounding box might
        // return a valid bounding box around annotations or datums
        if (object->hasExtension(App::GeoFeatureGroupExtension::getExtensionClassTypeId())) {
            continue;
        }

        // Get the bounding box of the object
        auto viewProvider = doc->getViewProvider(object);
        if (viewProvider && viewProvider->getBoundingBox().IsValid()) {
            return true;
        }
    }
    return false;
}

// When newDatum is the first value given to the one constraint that fixes
// the sketch's scale, scale the whole sketch about its origin to match it,
// so a sketch drawn freehand takes the size of its first dimension. Not
// done when the sketch already has a scale reference: external geometry,
// blocked geometry, or (by preference) a visible object in the document.
void EditDatumDialog::performAutoScale(double newDatum)
{
    performDatumAutoScale(sketch, ConstrNbr, newDatum);
}

void SketcherGui::performDatumAutoScale(Sketcher::SketchObject* sketch,
                                        int& ConstrNbr,
                                        double newDatum)
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning");
    long autoScaleMode = hGrp->GetInt(
        "AutoScaleMode", static_cast<int>(SketcherGui::AutoScaleMode::WhenNoScaleFeatureIsVisible));
    if (autoScaleMode == static_cast<int>(SketcherGui::AutoScaleMode::Never)) {
        return;
    }

    // the sketch's own document, not whichever is active
    Gui::Document* doc = Gui::Application::Instance->getDocument(sketch->getDocument());
    auto* vp = doc ? dynamic_cast<ViewProviderSketch*>(doc->getViewProvider(sketch)) : nullptr;
    if (!vp || !vp->isEditing()) {
        return;
    }

    if (autoScaleMode == static_cast<int>(SketcherGui::AutoScaleMode::WhenNoScaleFeatureIsVisible)
        && hasVisualFeature(sketch, nullptr, doc)) {
        return;
    }

    // External geometry beyond the two axes, or a blocked geometry, is a
    // scale reference the sketch was drawn against
    if (sketch->getExternalGeometryCount() > 2 || sketch->hasBlockConstraint()) {
        return;
    }

    // Only the one scale defining constraint triggers it, whatever number of
    // angles sit beside it
    if (sketch->getSingleScaleDefiningConstraint() != ConstrNbr) {
        return;
    }

    double oldDatum = sketch->getDatum(ConstrNbr);
    if (!std::isfinite(newDatum) || !std::isfinite(oldDatum)
        || std::abs(oldDatum) <= Precision::Confusion()) {
        return;
    }
    double scaleFactor = newDatum / oldDatum;
    if (!std::isfinite(scaleFactor) || scaleFactor <= Precision::Confusion()
        || std::abs(scaleFactor - 1.0) <= Precision::Confusion()) {
        return;
    }

    try {
        centerScale(vp, scaleFactor);

        // Constraints that cannot be scaled are dropped, so the datum
        // constraint may have moved
        int moved = sketch->getSingleScaleDefiningConstraint();
        if (moved >= 0) {
            ConstrNbr = moved;
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("Exception performing autoscale: %s\n", e.what());
    }
}
// ---------------------------------------------------------------------------
// The value typed in the view
// ---------------------------------------------------------------------------

namespace
{

ViewProviderSketch* editedViewProvider(Sketcher::SketchObject* sketch)
{
    Gui::Document* doc = Gui::Application::Instance->getDocument(sketch->getDocument());
    auto* vp = doc ? dynamic_cast<ViewProviderSketch*>(doc->getViewProvider(sketch)) : nullptr;
    return vp && vp->isEditing() ? vp : nullptr;
}

/// A view that can show no widget: a served client's mirror.
bool isWidgetless(const Gui::ViewerContext* viewer)
{
    return viewer && !viewer->datumEditorParent();
}

Base::Unit datumUnit(const Sketcher::Constraint* constraint)
{
    switch (constraint->Type) {
        case Sketcher::Angle:
            return Base::Unit::Angle;
        case Sketcher::Weight:
        case Sketcher::SnellsLaw:
            return Base::Unit();
        default:
            return Base::Unit::Length;
    }
}

/// What the dialog's Cancel does: the open command goes, the sketch is
/// what it was.
void rejectDatums(Sketcher::SketchObject* sketch)
{
    Gui::Command::abortCommand();
    sketch->recomputeFeature();
}

/// The path of a constraint's value as a command writes it
std::string constraintPath(Sketcher::SketchObject* sketch, int constraint)
{
    std::string path = sketch->Constraints.createPath(constraint).toEscapedString();
    if (!path.empty() && path[0] == '.') {
        path.erase(0, 1);
    }
    return path;
}

std::string pyString(const std::string& text)
{
    return Base::Tools::escapeEncodeString(QString::fromStdString(text)).toUtf8().constData();
}

}  // namespace

DatumEditSession::DatumEditSession(ViewProviderSketch* vp,
                                   Gui::ViewerContext* viewer,
                                   std::function<void(bool)> done)
    : vp(vp)
    , viewer(viewer)
    , done(std::move(done))
{}

DatumEditSession::~DatumEditSession() = default;

bool DatumEditSession::canEdit(ViewProviderSketch* vp, int constraint)
{
    const auto& all = vp->getSketchObject()->Constraints.getValues();
    if (constraint < 0 || constraint >= int(all.size()) || !all[constraint]->isDimensional()) {
        return false;
    }
    // Snell's law draws an icon, not a label: the editor stands at the
    // refraction point instead
    return all[constraint]->Type == Sketcher::SnellsLaw
        || vp->getConstraintDatumLabel(constraint);
}

SbVec3f DatumEditSession::anchorOf(int index) const
{
    if (Gui::SoDatumLabel* label = vp->getConstraintDatumLabel(index)) {
        return label->getLabelTextCenter();
    }
    Sketcher::SketchObject* sketch = vp->getSketchObject();
    const Sketcher::Constraint* constraint = sketch->Constraints.getValues()[index];
    Base::Vector3d point = sketch->getPoint(constraint->First, constraint->FirstPos);
    return {float(point.x), float(point.y), 0.0F};
}

bool DatumEditSession::start(ViewProviderSketch* vp,
                             Gui::ViewerContext* viewer,
                             const std::vector<int>& constraints,
                             std::function<void(bool)> done)
{
    Sketcher::SketchObject* sketch = vp->getSketchObject();

    // One at a time: whatever was being typed is taken as it stands. And
    // before anything below is looked up: applying it redraws the sketch,
    // which can free the labels (the auto scale drops constraints).
    if (vp->datumEdit) {
        vp->datumEdit->finish(true, false);
    }

    if (constraints.empty()) {
        return false;
    }
    for (int constraint : constraints) {
        if (!canEdit(vp, constraint)) {
            return false;
        }
    }

    auto* session = new DatumEditSession(vp, viewer, std::move(done));
    vp->datumEdit = session;

    // The command that made the constraint is still open, and stays open
    // until the value is in: the two are one undo step, and Std_Undo
    // takes the constraint back. A command's transaction is closed when the
    // command returns, which the dialog never let happen before the value
    // was in; this returns at once.
    session->ownTransaction = sketch->getDocument()->hasPendingTransaction();
    App::AutoTransaction::setEnable(false);
    // Everything the entry applies, value after value, is one undo step:
    // the command's, or this one. Opened for the document lazily, so an
    // entry that changes nothing leaves no step behind.
    if (!App::GetApplication().getActiveTransaction()) {
        Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Edit sketch datum"));
    }

    // Several at once (the Dimension tool's two) are visited in turn; one
    // alone visits every dimension the view shows, from that one.
    const auto& all = sketch->Constraints.getValues();
    if (constraints.size() > 1) {
        for (int constraint : constraints) {
            session->cycle.push_back(all[constraint]->getTag());
        }
    }

    auto placement = Base::Placement(vp->getDocument()->getEditingTransform());
    session->editor = std::make_unique<Gui::DatumValueEditor>(viewer, placement);
    session->editor->setDrivingIcons(
        Gui::BitmapFactory().iconFromTheme("Sketcher_Toggle_Constraint_Driving"),
        Gui::BitmapFactory().iconFromTheme("Sketcher_Toggle_Constraint_Driven"));
    // After the editor's own: Tab, Enter and Escape are the session's.
    session->editor->installKeyFilter(session);
    session->show(constraints.front());
    return true;
}

int DatumEditSession::indexOf(const boost::uuids::uuid& tag) const
{
    const auto& all = vp->getSketchObject()->Constraints.getValues();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i]->getTag() == tag) {
            return int(i);
        }
    }
    return -1;
}

int DatumEditSession::current() const
{
    return indexOf(editing);
}

bool DatumEditSession::isShown(int index) const
{
    if (!canEdit(vp, index)) {
        return false;
    }
    Sketcher::SketchObject* sketch = vp->getSketchObject();
    const Sketcher::Constraint* constraint = sketch->Constraints.getValues()[index];
    if (constraint->isInVirtualSpace != vp->getIsShownVirtualSpace() || !constraint->isVisible) {
        return false;
    }
    // on the screen
    auto placement = Base::Placement(vp->getDocument()->getEditingTransform());
    SbVec3f centre = anchorOf(index);
    Base::Vector3d point(centre[0], centre[1], centre[2]);
    placement.multVec(point, point);
    SbVec2s pixel =
        viewer->getPointOnViewport(SbVec3f(float(point.x), float(point.y), float(point.z)));
    SbVec2s size = viewer->getViewportRegion().getViewportSizePixels();
    return pixel[0] >= 0 && pixel[1] >= 0 && pixel[0] < size[0] && pixel[1] < size[1];
}

void DatumEditSession::show(int index)
{
    Sketcher::SketchObject* sketch = vp->getSketchObject();
    const Sketcher::Constraint* constraint = sketch->Constraints.getValues()[index];

    Gui::DatumValueEditor::Target target;
    target.label = vp->getConstraintDatumLabel(index);
    target.point = anchorOf(index);
    // an angle is shown and typed in degrees, as the dialog has it
    target.value = constraint->getValue();
    if (constraint->Type == Sketcher::Angle) {
        target.value = Base::toDegrees<double>(target.value);
    }
    target.unit = datumUnit(constraint);
    target.path = sketch->Constraints.createPath(index);
    if (auto info = sketch->getExpression(target.path); info.expression) {
        target.expression = QString::fromStdString(info.expression->toString());
    }
    // Snell's law is a ratio to meet, never a measurement
    target.driving = constraint->Type == Sketcher::SnellsLaw ? -1 : (constraint->isDriving ? 1 : 0);
    target.nameShown = true;
    target.name = QString::fromStdString(constraint->Name);

    editing = constraint->getTag();
    editor->edit(target);
}

bool DatumEditSession::applyCurrent()
{
    Sketcher::SketchObject* sketch = vp->getSketchObject();
    const int index = current();
    if (index < 0) {
        return false;
    }
    if (!editor->isModified()) {
        return true;
    }

    Gui::DatumValueEditor::Entry entry;
    QString why;
    // A reference's number is measured, and nothing in the line is applied
    // for it: what the line holds stops only a driving entry
    if (!editor->read(entry, &why) && entry.driving != 0) {
        editor->showError(why);
        return false;
    }
    const Sketcher::Constraint* constraint = sketch->Constraints.getValues()[index];
    const std::string name = entry.name.toStdString();
    if (name != constraint->Name && !SketcherGui::checkConstraintName(sketch, name)) {
        editor->showError(QObject::tr("Not a name an expression can use"));
        return false;
    }

    try {
        int target = index;
        const bool driving = entry.driving != 0;
        if (driving != constraint->isDriving) {
            Gui::cmdAppObjectArgs(sketch,
                                  "setDriving(%i, %s)",
                                  target,
                                  driving ? "True" : "False");
        }
        if (driving) {
            const std::string path = constraintPath(sketch, target);
            const bool bound = sketch->constraintHasExpression(target);
            if (entry.isExpression) {
                Gui::cmdAppObjectArgs(sketch,
                                      "setExpression('%s', u'%s')",
                                      path.c_str(),
                                      pyString(entry.expression->toString()).c_str());
            }
            else {
                if (bound) {
                    Gui::cmdAppObjectArgs(sketch, "setExpression('%s', None)", path.c_str());
                }
                // The first dimension of a sketch drawn freehand scales it.
                double newDatum = entry.value.getValue();
                performDatumAutoScale(sketch, target, newDatum);
                Gui::cmdAppObjectArgs(sketch,
                                      "setDatum(%i,App.Units.Quantity('%.15g %s'))",
                                      target,
                                      newDatum,
                                      pyString(entry.value.getUnit().getString()).c_str());
            }
        }
        if (name != sketch->Constraints.getValues()[target]->Name) {
            Gui::cmdAppObjectArgs(sketch,
                                  "renameConstraint(%d, u'%s')",
                                  target,
                                  pyString(name).c_str());
        }
        if (sketch->noRecomputes && sketch->ExpressionEngine.depsAreTouched()) {
            sketch->ExpressionEngine.execute();
            sketch->solve();
        }
        applied = true;
        // what the sketch has now is where typing starts again
        show(target);
        return true;
    }
    catch (const Base::Exception& e) {
        editor->showError(QString::fromUtf8(e.what()));
        // if setDatum failed, the solver's information is likely invalid
        if (sketch->noRecomputes) {
            sketch->solve();
        }
        return false;
    }
}

void DatumEditSession::move(int step)
{
    std::vector<int> visits;
    if (!cycle.empty()) {
        for (const auto& tag : cycle) {
            int index = indexOf(tag);
            if (canEdit(vp, index)) {
                visits.push_back(index);
            }
        }
    }
    else {
        const int count = int(vp->getSketchObject()->Constraints.getSize());
        for (int index = 0; index < count; ++index) {
            if (isShown(index)) {
                visits.push_back(index);
            }
        }
    }
    const int here = current();
    if (visits.empty() || here < 0) {
        return;
    }
    // From where the editor is, which need not be among them: a label
    // that went off the screen
    auto at = std::find(visits.begin(), visits.end(), here);
    int position = 0;
    if (at != visits.end()) {
        position = int(at - visits.begin()) + step;
    }
    else {
        auto after = std::upper_bound(visits.begin(), visits.end(), here);
        position = int(after - visits.begin()) + (step > 0 ? 0 : -1);
    }
    const int count = int(visits.size());
    position = ((position % count) + count) % count;
    show(visits[position]);
}

void DatumEditSession::finish(bool accept, bool notify)
{
    if (ended) {
        return;
    }
    if (accept && !applyCurrent()) {
        accept = false;
    }
    ended = true;

    Sketcher::SketchObject* sketch = vp->getSketchObject();
    // The editor goes first, off the screen now and deleted later: this is
    // called from inside it as a rule -- its key filter, or its
    // sendKeyEvent() for a key off the wire -- and that call has to return
    // into a live object.
    editor->close();
    editor.release()->deleteLater();
    if (vp->datumEdit == this) {
        vp->datumEdit = nullptr;
    }
    std::function<void(bool)> callback = std::move(done);
    deleteLater();

    if (accept) {
        Gui::Command::commitCommand();
        tryAutoRecompute(sketch);
    }
    else {
        rejectDatums(sketch);
    }
    if (notify && callback) {
        callback(accept);
    }
}

bool DatumEditSession::mouseButton(int button, bool pressed)
{
    // Elsewhere, that is: a press on the editor goes to the editor and
    // never reaches the view, and a served client's goes to its own page.
    if (button == 1 && pressed) {
        finish(true);
    }
    return true;
}

void DatumEditSession::documentRewound()
{
    if (ended) {
        return;
    }
    // Document::undo commits the open transaction before it undoes, so an
    // undo with one open has undone it -- the constraint the command made,
    // what was applied here -- and the document has moved past the entry.
    Sketcher::SketchObject* sketch = vp->getSketchObject();
    if ((ownTransaction || applied) && !sketch->getDocument()->hasPendingTransaction()) {
        finish(false);
        return;
    }
    // Otherwise the editor follows its constraint to wherever the list now
    // has it, what is typed kept; gone, the entry ends.
    const int index = current();
    if (!canEdit(vp, index)) {
        finish(false);
        return;
    }
    editor->follow(vp->getConstraintDatumLabel(index), sketch->Constraints.createPath(index));
}

bool DatumEditSession::eventFilter(QObject* watched, QEvent* event)
{
    if (ended) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
        case QEvent::KeyPress: {
            auto* key = static_cast<QKeyEvent*>(event);
            // a completion list open takes its own keys
            if (editor->isCompleting()) {
                break;
            }
            switch (key->key()) {
                case Qt::Key_Escape:
                    // Escape is Enter by default (ruled 2026-10-03): it
                    // leaves the entry with what was typed, as Escape
                    // leaves vim's insert mode, and taking an entry back
                    // is an undo -- Std_Undo while it runs, or one undo
                    // after, the entry being one step. The preference
                    // makes it the cancel it was.
                    if (App::GetApplication()
                            .GetParameterGroupByPath(
                                "User parameter:BaseApp/Preferences/Mod/Sketcher/General")
                            ->GetBool("DatumEscapeTakesBack", false)) {
                        finish(false);
                        return true;
                    }
                    [[fallthrough]];
                case Qt::Key_Return:
                case Qt::Key_Enter:
                    // what is typed, applied; not while it is no value
                    if (applyCurrent()) {
                        finish(true);
                    }
                    return true;
                case Qt::Key_Tab:
                case Qt::Key_Backtab:
                    if (applyCurrent()) {
                        move(key->key() == Qt::Key_Tab
                                     && !(key->modifiers() & Qt::ShiftModifier)
                                 ? 1
                                 : -1);
                    }
                    return true;
                default:
                    break;
            }
            break;
        }
        case QEvent::KeyRelease: {
            switch (static_cast<QKeyEvent*>(event)->key()) {
                case Qt::Key_Return:
                case Qt::Key_Enter:
                case Qt::Key_Escape:
                case Qt::Key_Tab:
                case Qt::Key_Backtab:
                    return true;
                default:
                    break;
            }
            break;
        }
        case QEvent::FocusOut: {
            // The focus gone elsewhere is the click elsewhere, for an
            // editor that is a widget. Not another window coming to the
            // front, or a menu, or the completer's list: nobody chose to
            // end the entry then.
            Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason == Qt::ActiveWindowFocusReason || reason == Qt::PopupFocusReason
                || reason == Qt::MenuBarFocusReason) {
                break;
            }
            // looked at once the focus has arrived where it is going
            QTimer::singleShot(0, this, [this]() {
                if (ended || editor->hasFocus() || editor->isCompleting()) {
                    return;
                }
                finish(true);
            });
            break;
        }
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

void SketcherGui::editDatums(Sketcher::SketchObject* sketch,
                             const std::vector<int>& constraints,
                             bool atCursor,
                             std::function<void(bool)> done)
{
    auto report = [&done](bool applied) {
        if (done) {
            done(applied);
        }
    };
    if (constraints.empty()) {
        report(false);
        return;
    }

    ViewProviderSketch* vp = editedViewProvider(sketch);
    Gui::ViewerContext* viewer = vp ? vp->getEditViewer() : nullptr;
    const bool widgetless = isWidgetless(viewer);

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/General");
    const bool inPlace = widgetless || hGrp->GetBool("EditDatumInPlace", true);

    if (vp && viewer && inPlace) {
        if (sketch->hasConflicts()) {
            Gui::TranslatedUserWarning(sketch,
                                       QObject::tr("Dimensional constraint"),
                                       QObject::tr("Not allowed to edit the datum because the "
                                                   "sketch contains conflicting constraints"));
            report(false);
            return;
        }
        if (DatumEditSession::start(vp, viewer, constraints, done)) {
            return;
        }
    }

    if (widgetless) {
        // No dialog for a view nobody sits at.
        Gui::TranslatedUserWarning(sketch,
                                   QObject::tr("Dimensional constraint"),
                                   QObject::tr("This value cannot be edited from here: its "
                                               "label is not shown."));
        rejectDatums(sketch);
        report(false);
        return;
    }

    bool applied = true;
    for (int constraint : constraints) {
        EditDatumDialog dialog(sketch, constraint);
        dialog.exec(atCursor);
        if (!dialog.isSuccess()) {
            applied = false;
            break;
        }
    }
    report(applied);
}

#include "moc_EditDatumDialog.cpp"
