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
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Quantity.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/EditableDatumLabel.h>
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

/// What the dialog's OK does with its value, for every one of them, in
/// one transaction.
bool applyDatums(Sketcher::SketchObject* sketch,
                 std::vector<std::pair<int, Base::Quantity>> values)
{
    App::AutoTransaction transaction("Edit sketch datum");
    try {
        for (auto& [constraint, quantity] : values) {
            const auto& constraints = sketch->Constraints.getValues();
            if (constraint < 0 || constraint >= int(constraints.size())
                || !constraints[constraint]->isDimensional()) {
                throw Base::ValueError("The constraint is not there any more");
            }
            double newDatum = quantity.getValue();
            // The first dimension of a sketch drawn freehand scales it.
            // With one value only: the scaling drops what it cannot scale,
            // and the other numbers would name other constraints.
            if (values.size() == 1) {
                performDatumAutoScale(sketch, constraint, newDatum);
            }
            Gui::cmdAppObjectArgs(sketch,
                                  "setDatum(%i,App.Units.Quantity('%.15g %s'))",
                                  constraint,
                                  newDatum,
                                  Base::Tools::escapeEncodeString(
                                      QString::fromStdString(quantity.getUnit().getString()))
                                      .toUtf8()
                                      .constData());
        }

        Gui::Command::commitCommand();

        if (sketch->noRecomputes && sketch->ExpressionEngine.depsAreTouched()) {
            sketch->ExpressionEngine.execute();
            sketch->solve();
        }

        tryAutoRecompute(sketch);
        return true;
    }
    catch (const Base::Exception& e) {
        Gui::NotifyUserError(sketch, QT_TRANSLATE_NOOP("Notifications", "Value Error"), e.what());

        Gui::Command::abortCommand();

        // if setDatum failed, the solver's information is likely invalid
        if (sketch->noRecomputes) {
            sketch->solve();
        }
        return false;
    }
}

}  // namespace

DatumEditSession::DatumEditSession(ViewProviderSketch* vp, std::function<void(bool)> done)
    : vp(vp)
    , done(std::move(done))
{}

DatumEditSession::~DatumEditSession() = default;

bool DatumEditSession::start(ViewProviderSketch* vp,
                             Gui::ViewerContext* viewer,
                             const std::vector<int>& constraints,
                             std::function<void(bool)> done)
{
    Sketcher::SketchObject* sketch = vp->getSketchObject();
    const auto& all = sketch->Constraints.getValues();

    std::vector<Gui::SoDatumLabel*> labels;
    for (int constraint : constraints) {
        Gui::SoDatumLabel* label = vp->getConstraintDatumLabel(constraint);
        if (!label) {
            return false;
        }
        labels.push_back(label);
    }

    // One at a time: whatever was being typed is taken as it stands.
    if (vp->datumEdit) {
        vp->datumEdit->finish(true, false);
    }

    auto* session = new DatumEditSession(vp, std::move(done));
    vp->datumEdit = session;

    // The command that made the constraint is still open, and stays open
    // until the value is in: the two are one undo step, and Escape takes
    // the constraint back. A command's transaction is closed when the
    // command returns, which the dialog never let happen before the value
    // was in; this returns at once.
    App::AutoTransaction::setEnable(false);

    auto placement = Base::Placement(vp->getDocument()->getEditingTransform());
    for (std::size_t i = 0; i < constraints.size(); ++i) {
        const Sketcher::Constraint* constraint = all[constraints[i]];
        Entry entry;
        entry.constraint = constraints[i];
        entry.label = std::make_unique<Gui::EditableDatumLabel>(viewer, placement);
        Gui::EditableDatumLabel* box = entry.label.get();
        box->setAnchorLabel(labels[i]);
        box->activate();

        // an angle is shown and typed in degrees, as the dialog has it
        double value = constraint->getValue();
        if (constraint->Type == Sketcher::Angle) {
            value = Base::toDegrees<double>(value);
        }
        // This object first among the box's filters: Enter, Escape and
        // Tab mean the session's here, not a tool parameter's. And the box
        // takes the mouse -- there is no tool it would be in the way of.
        box->startEdit(value, session, /*visibleToMouse = */ true);
        box->setSpinboxValue(value, datumUnit(constraint));
        session->entries.push_back(std::move(entry));
    }
    session->focus(0);
    return true;
}

void DatumEditSession::focus(int index)
{
    if (entries.empty()) {
        return;
    }
    const int count = int(entries.size());
    focused = ((index % count) + count) % count;
    Gui::EditableDatumLabel* box = entries[focused].label.get();
    box->setFocusToSpinbox();
    box->setFocus();
}

bool DatumEditSession::values(std::vector<std::pair<int, Base::Quantity>>& out) const
{
    const auto& all = vp->getSketchObject()->Constraints.getValues();
    for (const Entry& entry : entries) {
        Base::Quantity quantity;
        if (!entry.label->getQuantity(quantity)) {
            return false;
        }
        // a length, an angle; a plain number only where the constraint is
        // one (the dialog's rule)
        bool plainNumber = entry.constraint < int(all.size())
            && (all[entry.constraint]->Type == Sketcher::SnellsLaw
                || all[entry.constraint]->Type == Sketcher::Weight);
        if (!quantity.isQuantity() && !(plainNumber && quantity.isDimensionless())) {
            return false;
        }
        out.emplace_back(entry.constraint, quantity);
    }
    return true;
}

void DatumEditSession::finish(bool accept, bool notify)
{
    if (ended) {
        return;
    }
    std::vector<std::pair<int, Base::Quantity>> typed;
    if (accept && !values(typed)) {
        accept = false;
    }
    ended = true;

    Sketcher::SketchObject* sketch = vp->getSketchObject();
    // The boxes go first: applying a value redraws the sketch. Off the
    // screen now and deleted later: this is called from inside one of
    // them as a rule -- its event filter, or its sendKeyEvent() for a key
    // off the wire -- and that call has to return into a live object.
    for (Entry& entry : entries) {
        entry.label->deactivate();
        entry.label.release()->deleteLater();
    }
    entries.clear();
    if (vp->datumEdit == this) {
        vp->datumEdit = nullptr;
    }
    std::function<void(bool)> callback = std::move(done);
    // This runs inside one of the boxes' event filters as a rule.
    deleteLater();

    bool applied = false;
    if (accept) {
        applied = applyDatums(sketch, std::move(typed));
    }
    else {
        rejectDatums(sketch);
    }
    if (notify && callback) {
        callback(applied);
    }
}

bool DatumEditSession::mouseButton(int button, bool pressed)
{
    // Elsewhere, that is: a press on a box goes to the box and never
    // reaches the view, and a served client's goes to its own page.
    if (button == 1 && pressed) {
        finish(true);
    }
    return true;
}

bool DatumEditSession::eventFilter(QObject* watched, QEvent* event)
{
    if (ended) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
        case QEvent::KeyPress: {
            auto* key = static_cast<QKeyEvent*>(event);
            switch (key->key()) {
                case Qt::Key_Return:
                case Qt::Key_Enter: {
                    // every box as it stands; not while one holds no value
                    std::vector<std::pair<int, Base::Quantity>> typed;
                    if (values(typed)) {
                        finish(true);
                    }
                    return true;
                }
                case Qt::Key_Escape:
                    finish(false);
                    return true;
                case Qt::Key_Tab:
                    focus(focused + 1);
                    return true;
                case Qt::Key_Backtab:
                    focus(focused - 1);
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
        case QEvent::FocusIn: {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].label->hasFocus()) {
                    focused = int(i);
                }
            }
            break;
        }
        case QEvent::FocusOut: {
            // The focus gone elsewhere is the click elsewhere, for a box
            // that is a widget. Not another window coming to the front, or
            // a menu: nobody chose to end the entry then.
            Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason == Qt::ActiveWindowFocusReason || reason == Qt::PopupFocusReason
                || reason == Qt::MenuBarFocusReason) {
                break;
            }
            // looked at once the focus has arrived where it is going
            QTimer::singleShot(0, this, [this]() {
                if (ended) {
                    return;
                }
                for (const Entry& entry : entries) {
                    if (entry.label->hasFocus()) {
                        return;
                    }
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
                             std::function<void(bool)> done,
                             bool preferDialog)
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
    const bool inPlace = widgetless || (!preferDialog && hGrp->GetBool("EditDatumInPlace", true));

    // what a box can edit: a driving value that no expression gives
    const auto& all = sketch->Constraints.getValues();
    bool plain = true;
    for (int constraint : constraints) {
        if (constraint < 0 || constraint >= int(all.size()) || !all[constraint]->isDimensional()
            || !all[constraint]->isDriving || sketch->constraintHasExpression(constraint)) {
            plain = false;
        }
    }

    if (vp && viewer && inPlace && plain) {
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
                                   QObject::tr("This value cannot be edited from here: it is a "
                                               "reference, driven by an expression, or its label "
                                               "is not shown."));
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
