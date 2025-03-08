/****************************************************************************
 *   Copyright (c) 2019 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#include <QMessageBox>

#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/ExpressionParser.h>
#include <App/Range.h>
#include <Base/Tools.h>
#include <Base/ExceptionSafeCall.h>
#include <Gui/CommandT.h>
#include <Gui/PrefWidgets.h>
#include <Mod/Spreadsheet/App/SheetParams.h>

#include "DlgSheetConf.h"
#include "ui_DlgSheetConf.h"


using namespace App;
using namespace Spreadsheet;
using namespace SpreadsheetGui;

DlgSheetConf::DlgSheetConf(Sheet *sheet, Range range, QWidget *parent)
    : QDialog(parent)
    , sheet(sheet)
    , ui(new Ui::DlgSheetConf)
    , widgetStates(new Gui::PrefWidgetStates(this))
{
    ui->setupUi(this);

    if(range.colCount()==1) {
        auto to = range.to();
        if (SheetParams::getVerticalConfTable()) {
            to.setRow(CellAddress::MAX_ROWS-1);
            ui->checkBoxVertical->setChecked(true);
        }
        else {
            to.setCol(CellAddress::MAX_COLUMNS-1);
        }
        range = Range(range.from(),to);
        ui->checkBoxDoubleBind->setChecked(SheetParams::getDoubleBindConfTable());
    }
    else if (range.from().row() != range.to().row()) {
        ui->checkBoxVertical->setChecked(true);
    }

    ui->lineEditStart->setText(QString::fromUtf8(range.from().toString().c_str()));
    ui->lineEditEnd->setText(QString::fromUtf8(range.to().toString().c_str()));

    ui->lineEditProp->setDocumentObject(sheet,false);

    Base::connect(ui->btnDiscard, &QPushButton::clicked, this, &DlgSheetConf::onDiscard);

    CellAddress from,to;
    std::string rangeConf;
    ObjectIdentifier path;
    auto prop = prepare(from,to,rangeConf,path,true);
    if(prop) {
        ui->lineEditProp->setText(QString::fromUtf8(path.toString().c_str()));
        if (auto group = prop->getGroup())
            ui->lineEditGroup->setText(QString::fromUtf8(group));
    }

    ui->lineEditStart->setText(QString::fromUtf8(from.toString().c_str()));
    ui->lineEditEnd->setText(QString::fromUtf8(to.toString().c_str()));

    Base::connect(ui->checkBoxVertical, &QCheckBox::toggled, this, &DlgSheetConf::onChangeVertical);
}

DlgSheetConf::~DlgSheetConf()
{
    delete ui;
}

void DlgSheetConf::onChangeVertical(bool checked)
{
    SheetParams::setVerticalConfTable(checked);

    auto from = sheet->getCellAddress(
            ui->lineEditStart->text().trimmed().toUtf8().constData());
    auto to = sheet->getCellAddress(
            ui->lineEditEnd->text().trimmed().toUtf8().constData());
    if (checked && to.col() != from.col()) {
        to.setRow(to.col() == to.MAX_COLUMNS-1 ? to.MAX_ROWS-1 : to.col(), true);
        to.setCol(from.col(), true);
        ui->lineEditEnd->setText(QString::fromUtf8(to.toString().c_str()));
    }
    else if (!checked && to.row() != from.row()) {
        to.setCol(to.row() == to.MAX_ROWS-1 ? to.MAX_COLUMNS-1 : to.row(), true);
        to.setRow(from.row(), true);
        ui->lineEditEnd->setText(QString::fromUtf8(to.toString().c_str()));
    }
}

App::Property *DlgSheetConf::prepare(CellAddress &from, CellAddress &to,
    std::string &rangeConf, ObjectIdentifier &path, bool init)
{
    from = sheet->getCellAddress(
            ui->lineEditStart->text().trimmed().toUtf8().constData());
    to = sheet->getCellAddress(
            ui->lineEditEnd->text().trimmed().toUtf8().constData());

    CellAddress confFrom;

    bool vertical = ui->checkBoxVertical->isChecked();
    if (!vertical) {
        // Setup row as parameters, and column as configurations
        if(from.col()>=to.col())
            FC_THROWM(Base::RuntimeError, "Invalid cell range");
        to.setRow(from.row());
        confFrom = CellAddress(from.row()+1,from.col());
        rangeConf = confFrom.toString();
        // rangeConf is supposed to hold the range of string cells, each
        // holding the name of a configuration. The '|' below indicates a
        // growing but continuous column, so that we can auto include new
        // configurations. We'll bind the string list to a
        // PropertyEnumeration for dynamical switching of the
        // configuration.
        rangeConf += ":|";
    }
    else {
        if(from.row()>=to.row())
            FC_THROWM(Base::RuntimeError, "Invalid cell range");
        to.setCol(from.col());
        confFrom = CellAddress(from.row(),from.col()+1);
        rangeConf = confFrom.toString();
        rangeConf += ":-";
    }

    if(!init) {
        std::string exprTxt(ui->lineEditProp->text().trimmed().toUtf8().constData());
        ExpressionPtr expr;
        try {
            expr = App::Expression::parse(sheet,exprTxt);
        } catch (Base::Exception &e) {
            e.ReportException();
            FC_THROWM(Base::RuntimeError, "Failed to parse expression for property");
        }
        if(expr->hasComponent() || !expr->isDerivedFrom(App::VariableExpression::getClassTypeId()))
            FC_THROWM(Base::RuntimeError, "Invalid property expression: " << expr->toString());

        path = static_cast<App::VariableExpression*>(expr.get())->getPath();
        auto obj = path.getDocumentObject();
        if(!obj)
            FC_THROWM(Base::RuntimeError, "Invalid object referenced in: " << expr->toString());

        int pseudoType;
        auto prop = path.getProperty(&pseudoType);
        if(pseudoType || (prop && (!prop->isDerivedFrom(App::PropertyEnumeration::getClassTypeId())
                                    || !prop->testStatus(App::Property::PropDynamic))))
        {
            FC_THROWM(Base::RuntimeError, "Invalid property referenced in: " << expr->toString());
        }
        return prop;
    }

    Cell *cell = sheet->getCell(from);
    if(!cell)
        return nullptr;
    auto vexpr = VariableExpression::isDoubleBinding(cell->getExpression());
    if (!vexpr) {
        if (auto lexpr = SimpleStatement::cast<ListExpression>(cell->getExpression())) {
            if (lexpr->getSize() == 2) {
                vexpr = VariableExpression::isDoubleBinding(lexpr->getItems()[1].get());
            }
        }
        else if (auto fexpr = SimpleStatement::cast<FunctionExpression>(cell->getExpression())) {
            if((fexpr->type()==FunctionExpression::HREF
                        || fexpr->type() == FunctionExpression::HIDDEN_REF)
                    && fexpr->getArgs().size()==1)
                vexpr = Base::freecad_dynamic_cast<VariableExpression>(fexpr->getArgs().front().get());
        }
    }

    if(vexpr) {
        if (init) {
            ui->checkBoxDoubleBind->setChecked(true);
        }

        auto prop = Base::freecad_dynamic_cast<PropertyEnumeration>(
                            vexpr->getPath().getProperty());
        if(prop && prop->hasName()) {
            auto obj = Base::freecad_dynamic_cast<DocumentObject>(prop->getContainer());
            if(obj) {
                path = ObjectIdentifier(sheet);
                path.setDocumentObjectName(obj,true);
                path << ObjectIdentifier::SimpleComponent(prop->getName());
                if (init) {
                    ui->lineEditProp->setText(QString::fromUtf8(path.toString().c_str()));
                }
                return prop;
            }
        }
    }
    return nullptr;
}

void DlgSheetConf::accept()
{
    bool commandActive = false;
    try {
        std::string rangeConf;
        CellAddress from,to;
        ObjectIdentifier path;
        App::Property *prop = prepare(from,to,rangeConf,path,false);

        bool vertical = ui->checkBoxVertical->isChecked();

        Range range(from,to);

        // check rangeConf, make sure it is a sequence of string only
        Range r(sheet->getRange(rangeConf.c_str()));
        do {
            auto cell = sheet->getCell(*r);
            if(cell && cell->getExpression()) {
                ExpressionPtr expr(cell->getExpression()->eval());
                if(expr->isDerivedFrom(StringExpression::getClassTypeId()))
                    continue;
            }
            FC_THROWM(Base::RuntimeError, "Expects cell "
                    << r.address() << " evaluates to string.\n"
                    << rangeConf << " is supposed to contain a list of configuration names");
        } while(r.next());

        std::string exprTxt(ui->lineEditProp->text().trimmed().toUtf8().constData());
        App::ExpressionPtr expr(App::Expression::parse(sheet,exprTxt));
        if(expr->hasComponent() || !expr->isDerivedFrom(App::VariableExpression::getClassTypeId()))
            FC_THROWM(Base::RuntimeError, "Invalid property expression: " << expr->toString());

        AutoTransaction guard("Setup conf table");
        commandActive = true;

        // unbind any previous binding
        int count = range.rowCount() * range.colCount();
        for (int i=0; i<count; ++i) {
            auto r = range;
            auto binding = sheet->getCellBinding(r);
            if(!binding)
                break;
            Gui::cmdAppObjectArgs(sheet, "setExpression('.cells.%s.%s.%s', None)",
                    binding==PropertySheet::BindingNormal?"Bind":"BindHiddenRef",
                    r.from().toString(), r.to().toString());
        }

        auto obj = path.getDocumentObject();
        if(!obj)
            FC_THROWM(Base::RuntimeError, "Object not found");

        // Add a dynamic PropertyEnumeration for user to switch the configuration
        std::string propName = path.getPropertyName();
        QString groupName = ui->lineEditGroup->text().trimmed();
        if(!prop) {
            prop = obj->addDynamicProperty("App::PropertyEnumeration", propName.c_str(),
                                groupName.toUtf8().constData());
        } else if (groupName.size())
            obj->changeDynamicProperty(prop, groupName.toUtf8().constData(), nullptr);
        prop->setStatus(App::Property::CopyOnChange,true);

        // Bind the enumeration items to the column of configuration names
        Gui::cmdAppObjectArgs(obj, "setExpression('%s.Enum', '%s.cells[<<%s>>]')",
                propName, sheet->getFullName(), rangeConf);

        Gui::cmdAppObjectArgs(obj, "recompute()");

        // Adjust the range to skip the first cell
        if (vertical)
            range = Range(from.row()+1,from.col(),to.row(),to.col());
        else
            range = Range(from.row(),from.col()+1,to.row(),to.col());

        // Formulate expression to calculate the row binding using
        // PropertyEnumeration
        if (vertical) {
            Gui::cmdAppObjectArgs(sheet, "setExpression('.cells.Bind.%s.%s', "
                "'tuple(.cells, (%d, hiddenref(%s)+%d), (%d, hiddenref(%s)+%d))')",
                range.from().toString(CellAddress::Cell::ShowRowColumn),
                range.to().toString(CellAddress::Cell::ShowRowColumn),
                range.from().row(),
                prop->getFullName(),
                range.from().col()+1,
                range.to().row(),
                prop->getFullName(),
                range.to().col()+1);
        }
        else {
            Gui::cmdAppObjectArgs(sheet, "setExpression('.cells.Bind.%s.%s', "
                "'tuple(.cells, <<%s>> + str(hiddenref(%s)+%d), <<%s>> + str(hiddenref(%s)+%d))')",
                range.from().toString(CellAddress::Cell::ShowRowColumn),
                range.to().toString(CellAddress::Cell::ShowRowColumn),
                range.from().toString(CellAddress::Cell::ShowColumn),
                prop->getFullName(),
                from.row()+2,
                prop->getFullName(),
                from.row()+2);
        }

        if (ui->checkBoxDoubleBind->isChecked()) {
            Gui::cmdAppObjectArgs(sheet, "setPersistentEdit('%s', False)",
                from.toString(CellAddress::Cell::ShowRowColumn));
            Gui::cmdAppObjectArgs(sheet, "setEditMode('%s', 'Normal')",
                from.toString(CellAddress::Cell::ShowRowColumn));
            Gui::cmdAppObjectArgs(sheet, "set('%s', '=(.cells[<<%s>>], dbind(%s))')",
                from.toString(CellAddress::Cell::ShowRowColumn),
                rangeConf,
                prop->getFullName());

            Gui::Command::doCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
            Gui::cmdAppObjectArgs(sheet, "setEditMode('%s', 'Combo')",
                from.toString(CellAddress::Cell::ShowRowColumn));
            Gui::cmdAppObjectArgs(sheet, "setPersistentEdit('%s', True)",
                from.toString(CellAddress::Cell::ShowRowColumn));
        }

        Gui::Command::doCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
        Gui::Command::commitCommand();
        QDialog::accept();
    } catch(Base::Exception &e) {
        e.ReportException();
        QMessageBox::critical(this, tr("Setup configuration table"), QString::fromUtf8(e.what()));
        if(commandActive)
            Gui::Command::abortCommand();
    }
}

void DlgSheetConf::onDiscard() {
    bool commandActive = false;
    try {
        std::string rangeConf;
        CellAddress from,to;
        ObjectIdentifier path;
        auto prop = prepare(from,to,rangeConf,path,true);

        Range range(from,to);

        AutoTransaction guard("Unsetup conf table");
        commandActive = true;

        // unbind any previous binding
        int count = range.rowCount() * range.colCount();
        for (int i=0; i<count; ++i) {
            auto r = range;
            auto binding = sheet->getCellBinding(r);
            if(!binding)
                break;
            Gui::cmdAppObjectArgs(sheet, "setExpression('.cells.%s.%s.%s', None)",
                    binding==PropertySheet::BindingNormal?"Bind":"BindHiddenRef",
                    r.from().toString(), r.to().toString());
        }

        Gui::cmdAppObjectArgs(sheet, "clear('%s')", from.toString(CellAddress::Cell::ShowRowColumn));

        if(prop && prop->hasName()) {
            auto obj = path.getDocumentObject();
            if(!obj)
                FC_THROWM(Base::RuntimeError, "Object not found");
            Gui::cmdAppObjectArgs(obj, "setExpression('%s.Enum', None)", prop->getName());
            if(prop->testStatus(Property::PropDynamic))
                Gui::cmdAppObjectArgs(obj, "removeProperty('%s')", prop->getName());
        }

        Gui::Command::doCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
        Gui::Command::commitCommand();
        QDialog::accept();
    } catch(Base::Exception &e) {
        e.ReportException();
        QMessageBox::critical(this, tr("Unsetup configuration table"), QString::fromUtf8(e.what()));
        if(commandActive)
            Gui::Command::abortCommand();
    }
}

#include "moc_DlgSheetConf.cpp"
