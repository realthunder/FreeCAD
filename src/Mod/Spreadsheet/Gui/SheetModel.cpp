/***************************************************************************
 *   Copyright (c) 2015 Eivind Kvedalen <eivind@kvedalen.name>             *
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
#include <QFont>
#include <QLocale>
#endif

#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <Base/Interpreter.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Mod/Spreadsheet/App/Sheet.h>
#include <Mod/Spreadsheet/App/SheetParams.h>
#include <Mod/Spreadsheet/App/Cell.h>
#include <Mod/Spreadsheet/App/Utils.h>

#include "SheetModel.h"


using namespace SpreadsheetGui;
using namespace Spreadsheet;
using namespace App;
namespace bp = boost::placeholders;

SheetModel::SheetModel(Sheet* _sheet, QObject* parent) : QAbstractTableModel(parent), sheet(_sheet)
{
    cellUpdatedConnection = sheet->cellUpdated.connect(bind(&SheetModel::cellUpdated, this, bp::_1));
    rangeUpdatedConnection = sheet->rangeUpdated.connect(bind(&SheetModel::rangeUpdated, this, bp::_1));
    tableRefreshConnection = sheet->tableRefresh.connect([this]() {
        beginResetModel();
        endResetModel();
    });

    aliasBgColor = QColor(Base::Tools::fromStdString(SheetParams::getAliasedCellBackgroundColor()));
    lockedAliasColor = QColor(Base::Tools::fromStdString(SheetParams::getLockedAliasedCellColor()));
    setForegroundColor(QColor(Base::Tools::fromStdString(SheetParams::getTextColor())));
}

SheetModel::~SheetModel()
{
    cellUpdatedConnection.disconnect();
    rangeUpdatedConnection.disconnect();
}

QColor SheetModel::foregroundColor() const
{
    return textFgColor;
}

QColor SheetModel::getForeground(const Spreadsheet::Cell *cell, int number) const
{
    Color color;
    if (cell->getForeground(color))
        return QColor(255.0 * color.r, 255.0 * color.g, 255.0 * color.b, 255.0 * color.a);
    else if (cell->hasAlias() && (aliasFgColor.rgb() & 0xff))
        return aliasFgColor;
    else if (number < 0)
        return negativeFgColor;
    else if (number > 0)
        return positiveFgColor;
    else
        return textFgColor;
}

void SheetModel::setForegroundColor(const QColor &c)
{
    textFgColor = c;
    std::string color = SheetParams::getPositiveNumberColor();
    positiveFgColor = color.empty() ? textFgColor : QColor(Base::Tools::fromStdString(color));
    color = SheetParams::getNegativeNumberColor();
    negativeFgColor = color.empty() ? textFgColor: QColor(Base::Tools::fromStdString(color));
}

QColor SheetModel::aliasForegroundColor() const
{
    return aliasFgColor;
}

void SheetModel::setAliasForegroundColor(const QColor &c)
{
    aliasFgColor = c;
}

int SheetModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 16384;
}

int SheetModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 26 * 26 + 26;
}

namespace
{
QVariant formatCellDisplay(QString value, const Cell* cell)
{
    std::string alias;
    if (cell->getAlias(alias) && SheetParams::getshowAliasName()) {
        QString formatStr = QString::fromStdString(SheetParams::getDisplayAliasFormatString());
        if (formatStr.contains(QStringLiteral("%V")) || formatStr.contains(QStringLiteral("%A"))) {
            formatStr.replace(QStringLiteral("%A"), QString::fromStdString(alias));
            formatStr.replace(QStringLiteral("%V"), value);
            return QVariant(formatStr);
        }
    }
    return QVariant(value);
}
}// namespace

QVariant SheetModel::data(const QModelIndex& index, int role) const
{
    static const Cell* emptyCell = new Cell(CellAddress(0, 0), nullptr);
    int row = index.row();
    int col = index.column();
    const Cell* cell = sheet->getCell(CellAddress(row, col));

    if (!cell)
        cell = emptyCell;

//#define DEBUG_DEPS
#ifdef DEBUG_DEPS
    if (role == Qt::ToolTipRole) {
        QString v;

        std::set<std::string> deps = sheet->dependsOn(CellAddress(row, col));
        std::set<std::string> provides;

        sheet->providesTo(CellAddress(row, col), provides);

        if (deps.size() > 0) {
            v += QString::fromUtf8("Depends on:");
            for (std::set<std::string>::const_iterator i = deps.begin(); i != deps.end(); ++i)
                v += QString::fromUtf8("\n\t") + Tools::fromStdString(*i);
            v += QString::fromUtf8("\n");
        }
        if (provides.size() > 0) {
            v += QString::fromUtf8("Used by:");
            for (std::set<std::string>::const_iterator i = provides.begin(); i != provides.end();
                 ++i)
                v += QString::fromUtf8("\n\t") + Tools::fromStdString(*i);
            v += QString::fromUtf8("\n");
        }
        return QVariant(v);
    }
#else
    if (!cell->hasException() && role == Qt::ToolTipRole) {
        std::string alias;
        if (cell->getAlias(alias))
            return QVariant(Base::Tools::fromStdString(alias));
    }
#endif

    if (cell->hasException()) {
        switch (role) {
            case Qt::ToolTipRole: {
                QString txt(Base::Tools::fromStdString(cell->getException()).toHtmlEscaped());
                return QVariant(QStringLiteral("<pre>%1</pre>").arg(txt));
            }
            case Qt::DisplayRole: {
#ifdef DEBUG_DEPS
                return QVariant::fromValue(
                    QString::fromUtf8("#ERR: %1").arg(Tools::fromStdString(cell->getException())));
#else
                std::string str;
                if (cell->getStringContent(str))
                    return QVariant::fromValue(QString::fromUtf8(str.c_str()));
                return QVariant::fromValue(QString::fromUtf8("#ERR"));
#endif
            }
            case Qt::ForegroundRole:
                return QVariant::fromValue(QColor(255.0, 0, 0));
            case Qt::TextAlignmentRole:
                return QVariant(Qt::AlignVCenter | Qt::AlignLeft);
            default:
                break;
        }
    }

    // Get edit value by querying the sheet
    if (role == Qt::EditRole) {
        try {
            return cell->getEditData(true);
        } catch (Base::Exception &e) {
            e.ReportException();
        }
        return QVariant();
    }

    if(cell == emptyCell)
        return QVariant();

    // Get display value as computed property
    std::string address = CellAddress(row, col).toString();
    Property* prop = sheet->getPropertyByName(address.c_str());

    if (role == Qt::BackgroundRole) {
        Color color;

        if (cell->getBackground(color))
            return QVariant::fromValue(QColor(255.0 * color.r, 255.0 * color.g, 255.0 * color.b, 255.0 * color.a));
        else if (cell->hasAlias())
            return QVariant::fromValue(cell->isAliasLocked() ? lockedAliasColor : aliasBgColor);
        else
            return QVariant();
    }

    int qtAlignment = 0;

    int alignment;
    cell->getAlignment(alignment);

    if (alignment & Cell::ALIGNMENT_LEFT)
        qtAlignment |= Qt::AlignLeft;
    if (alignment & Cell::ALIGNMENT_HCENTER)
        qtAlignment |= Qt::AlignHCenter;
    if (alignment & Cell::ALIGNMENT_RIGHT)
        qtAlignment |= Qt::AlignRight;
    if (alignment & Cell::ALIGNMENT_TOP)
        qtAlignment |= Qt::AlignTop;
    if (alignment & Cell::ALIGNMENT_VCENTER)
        qtAlignment |= Qt::AlignVCenter;
    if (alignment & Cell::ALIGNMENT_BOTTOM)
        qtAlignment |= Qt::AlignBottom;

    std::set<std::string> style;
    if (role == Qt::FontRole && cell->getStyle(style)) {
        QFont f;

        for (std::set<std::string>::const_iterator i = style.begin(); i != style.end(); ++i) {
            if (*i == "bold")
                f.setBold(true);
            else if (*i == "italic")
                f.setItalic(true);
            else if (*i == "underline")
                f.setUnderline(true);
        }

        return QVariant::fromValue(f);
    }

    auto dirtyCells = sheet->getCells()->getDirty();
    auto dirty = (dirtyCells.find(CellAddress(row, col)) != dirtyCells.end());

    if (!prop || dirty) {
        switch (role) {
            case  Qt::ForegroundRole: {
                return QColor(0, 0, 255.0); // TODO: Remove this hardcoded color, replace with preference
            }
            case Qt::TextAlignmentRole: {
                qtAlignment = Qt::AlignHCenter | Qt::AlignVCenter;
                return QVariant::fromValue(qtAlignment);
            }
            case Qt::DisplayRole:
                if(cell->getExpression()) {
                    std::string str;
                    if (cell->getStringContent(str)) {
                        if (!str.empty() && str[0] == '=')
                            // If this is a real computed value, indicate that a recompute is
                            // needed before we can display it
                            return QVariant(QStringLiteral("#PENDING"));
                        else
                            // If it's just a simple value, display the new value, but still
                            // format it as a pending value to indicate to the user that
                            // a recompute is needed
                            return QVariant(QString::fromUtf8(str.c_str()));
                    }
                    else
                        return QVariant();
                }
                else
                    return QVariant();
            default:
                return QVariant();
        }
    }
    else if (prop->isDerivedFrom(App::PropertyString::getClassTypeId())) {
        /* String */
        const App::PropertyString* stringProp = static_cast<const App::PropertyString*>(prop);

        switch (role) {
            case Qt::ForegroundRole:
                return getForeground(cell);
            case Qt::TextAlignmentRole: {
                if (alignment & Cell::ALIGNMENT_HIMPLIED) {
                    qtAlignment &= ~(Qt::AlignLeft | Qt::AlignHCenter | Qt::AlignRight);
                    qtAlignment |= Qt::AlignLeft;
                }
                if (alignment & Cell::ALIGNMENT_VIMPLIED) {
                    qtAlignment &= ~(Qt::AlignTop | Qt::AlignVCenter | Qt::AlignBottom);
                    qtAlignment |= Qt::AlignVCenter;
                }
                return QVariant::fromValue(qtAlignment);
            }
            case Qt::DisplayRole: {
                QString v = QString::fromUtf8(stringProp->getValue());
                return formatCellDisplay(v, cell);
            }
            default:
                return QVariant();
        }
    }
    else if (prop->isDerivedFrom(App::PropertyQuantity::getClassTypeId())) {
        /* Number */
        const App::PropertyQuantity* floatProp = static_cast<const App::PropertyQuantity*>(prop);

        switch (role) {
            case  Qt::ForegroundRole: {
                const Base::Unit & computedUnit = floatProp->getUnit();
                DisplayUnit displayUnit;
                if (cell->getDisplayUnit(displayUnit) &&
                        !computedUnit.isEmpty() && computedUnit != displayUnit.unit) {
                    return QVariant::fromValue(QColor(255.0, 0, 0));
                }
                return getForeground(cell, floatProp->getValue() < 0 ? -1 : 1);
            }
            case Qt::TextAlignmentRole: {
                if (alignment & Cell::ALIGNMENT_HIMPLIED) {
                    qtAlignment &= ~(Qt::AlignLeft | Qt::AlignHCenter | Qt::AlignRight);
                    qtAlignment |= Qt::AlignRight;
                }
                if (alignment & Cell::ALIGNMENT_VIMPLIED) {
                    qtAlignment &= ~(Qt::AlignTop | Qt::AlignVCenter | Qt::AlignBottom);
                    qtAlignment |= Qt::AlignVCenter;
                }
                return QVariant::fromValue(qtAlignment);
            }
            case Qt::DisplayRole: {
                QString v;
                const Base::Unit& computedUnit = floatProp->getUnit();
                DisplayUnit displayUnit;

                // Display locale specific decimal separator (#0003875,#0003876)
                if (cell->getDisplayUnit(displayUnit)) {
                    if (computedUnit.isEmpty() || computedUnit == displayUnit.unit) {
                        QString number =
                            QLocale().toString(floatProp->getValue() / displayUnit.scaler, 'f',
                                               Base::UnitsApi::getDecimals());
                        //QString number = QString::number(floatProp->getValue() / displayUnit.scaler);
                        v = number + Base::Tools::fromStdString(" " + displayUnit.stringRep);
                    }
                    else {
                        v = QString::fromUtf8("#ERR: display unit mismatch");
                        return QVariant(v);
                    }
                }
                else {

                    // When displaying a quantity then use the globally set scheme
                    // See: https://forum.freecad.org/viewtopic.php?f=3&t=50078
                    Base::Quantity value = floatProp->getQuantityValue();
                    v = value.getUserString();
                }
                return formatCellDisplay(v, cell);
            }
            default:
                return QVariant();
        }
    }
    else if (prop->isDerivedFrom(App::PropertyFloat::getClassTypeId())
             || prop->isDerivedFrom(App::PropertyInteger::getClassTypeId())) {
        /* Number */
        double d;
        long l;
        bool isInteger = false;
        if (prop->isDerivedFrom(App::PropertyFloat::getClassTypeId()))
            d = static_cast<const App::PropertyFloat*>(prop)->getValue();
        else {
            isInteger = true;
            l = static_cast<const App::PropertyInteger*>(prop)->getValue();
            d = l;
        }

        switch (role) {
            case  Qt::ForegroundRole:
                return getForeground(cell, d < 0 ? -1 : 1);
            case Qt::TextAlignmentRole: {
                if (alignment & Cell::ALIGNMENT_HIMPLIED) {
                    qtAlignment &= ~(Qt::AlignLeft | Qt::AlignHCenter | Qt::AlignRight);
                    qtAlignment |= Qt::AlignRight;
                }
                if (alignment & Cell::ALIGNMENT_VIMPLIED) {
                    qtAlignment &= ~(Qt::AlignTop | Qt::AlignVCenter | Qt::AlignBottom);
                    qtAlignment |= Qt::AlignVCenter;
                }
                return QVariant::fromValue(qtAlignment);
            }
            case Qt::DisplayRole: {
                QString v;
                DisplayUnit displayUnit;

                // Display locale specific decimal separator (#0003875,#0003876)
                if (cell->getDisplayUnit(displayUnit)) {
                    QString number = QLocale().toString(d / displayUnit.scaler, 'f',
                                                        Base::UnitsApi::getDecimals());
                    //QString number = QString::number(d / displayUnit.scaler);
                    v = number + Base::Tools::fromStdString(" " + displayUnit.stringRep);
                }
                else if (!isInteger) {
                    v = QLocale::system().toString(d, 'f', Base::UnitsApi::getDecimals());
                    //v = QString::number(d);
                }
                else
                    v = QString::number(l);
                return formatCellDisplay(v, cell);
            }
            default:
                return QVariant();
        }
    }
    else {
        switch (role) {
            case  Qt::ForegroundRole:
                return getForeground(cell);
            case Qt::TextAlignmentRole: {
                if (alignment & Cell::ALIGNMENT_HIMPLIED) {
                    qtAlignment &= ~(Qt::AlignLeft | Qt::AlignHCenter | Qt::AlignRight);
                    qtAlignment |= Qt::AlignHCenter;
                }
                if (alignment & Cell::ALIGNMENT_VIMPLIED) {
                    qtAlignment &= ~(Qt::AlignTop | Qt::AlignVCenter | Qt::AlignBottom);
                    qtAlignment |= Qt::AlignVCenter;
                }
                return QVariant::fromValue(qtAlignment);
            }
            case Qt::DisplayRole: {
                Base::PyGILStateLocker lock;
                std::string value;
                try {
                    if(cell->getEditMode())
                        return cell->getDisplayData(true);
                    PropertyString tmp;
                    tmp.setPyObject(Py::asObject(prop->getPyObject()).ptr());
                    value = tmp.getValue();
                    QString v = QString::fromUtf8(value.c_str());
                    return formatCellDisplay(v, cell);
                }
                catch (Py::Exception&) {
                    Base::PyException e;
                    value = "#ERR: ";
                    value += e.what();
                }
                catch (Base::Exception& e) {
                    value = "#ERR: ";
                    value += e.what();
                }
                catch (...) {
                    value = "#ERR: unknown exception";
                }
                return QVariant(QString::fromUtf8(value.c_str()));
            }
            default:
                return QVariant();
        }
    }

    return QVariant();
}

QVariant SheetModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::SizeHintRole) {
        if (orientation == Qt::Horizontal)
            return QVariant(
                QSize(sheet->getColumnWidth(section), PropertyRowHeights::defaultHeight));
        else
            return QVariant(
                QSize(PropertyColumnWidths::defaultHeaderWidth, sheet->getRowHeight(section)));
    }
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            static QString labels = QString::fromUtf8("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
            if (section < 26) {
                return QVariant(labels[section]);
            }
            else {
                section -= 26;
                return QVariant(QString(labels[section / 26]) + QString(labels[section % 26]));
            }
        }
        else {
            return QString::number(section + 1);
        }
    } else if (role == Qt::ForegroundRole) {
        if((orientation == Qt::Horizontal && sheet->hiddenColumns.getValues().count(section)) ||
           (orientation == Qt::Vertical && sheet->hiddenRows.getValues().count(section)))
           return QColor(0,0,255);
    }
    return QVariant();
}

bool SheetModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
    if (role == Qt::DisplayRole) {
        // Nothing to do, it will get updated by the sheet in the application logic
    }
    else if (role == Qt::EditRole || role == Qt::UserRole) {
        CellAddress address(index.row(), index.column());
        std::ostringstream ss;
        ss << tr("Edit cell").toUtf8().constData() << " " << address.toString();
        if (const auto * cell = sheet->getCell(address)) {
            std::string str;
            if (cell->getAlias(str))
                ss << " (" << str << ")";
        }
        App::AutoTransaction guard(ss.str().c_str());
        try {
            if (role == Qt::UserRole) {
                sheet->setCell(address, value.toString().toUtf8().constData());
            }
            else if(!sheet->editCell(address, value))
                return true;
            Gui::Command::doCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
        } catch (Base::Exception & e) {
            e.ReportException();
            guard.close(true);
            return false;
        }
    }
    return true;
}

Qt::ItemFlags SheetModel::flags(const QModelIndex& /*index*/) const
{
    return Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled;
}

void SheetModel::cellUpdated(CellAddress address)
{
    QModelIndex i = index(address.row(), address.col());

    Q_EMIT dataChanged(i, i);
}

void SheetModel::rangeUpdated(const Range& range)
{
    QModelIndex i = index(range.from().row(), range.from().col());
    QModelIndex j = index(range.to().row(), range.to().col());

    Q_EMIT dataChanged(i, j);
}

#include "moc_SheetModel.cpp"
