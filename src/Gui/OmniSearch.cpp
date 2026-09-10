/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef _PreComp_
# include <climits>
# include <cstdlib>
# include <cstring>
# include <QCoreApplication>
#endif

#include <App/Application.h>
#include <App/DocumentObject.h>
#include <App/ParamRegistry.h>
#include <App/Property.h>
#include <Base/Exception.h>

#include "OmniSearch.h"
#include "Command.h"
#include "CommandCompleter.h"
#include "PrefWidgets.h"

using namespace Gui;
using App::ParamInfo;
using App::ParamRegistry;

// ---------------------------------------------------------------------------
// the grammar

const char *OmniSearch::modePrefix(Mode mode)
{
    switch (mode) {
    case Mode::Object:
        return "/ ";
    case Mode::Command:
        return "/cmd ";
    case Mode::Param:
        return "/param ";
    case Mode::Chooser:
        break;
    }
    return "/";
}

OmniSearch::Input OmniSearch::parseInput(const QString &text)
{
    Input res;
    if (!text.startsWith(QLatin1Char('/'))) {
        res.mode = Mode::Object;
        res.query = text;
        res.offset = 0;
        return res;
    }
    for (Mode mode : {Mode::Command, Mode::Param, Mode::Object}) {
        QString prefix = QString::fromLatin1(modePrefix(mode));
        if (text.startsWith(prefix)) {
            res.mode = mode;
            res.offset = prefix.size();
            res.query = text.mid(res.offset);
            return res;
        }
    }
    res.mode = Mode::Chooser;
    res.query = text;
    res.offset = 0;
    return res;
}

// ---------------------------------------------------------------------------
// objects

bool OmniSearch::resolveObject(const QString &query, App::DocumentObject *owner, ObjectMatch &out)
{
    if (!owner || !owner->isAttachedToDocument())
        return false;
    std::string txt = query.trimmed().toUtf8().constData();
    if (txt.empty())
        return false;

    // A property reference first: "Box.Length", "Part.Box.Placement".
    try {
        auto path = App::ObjectIdentifier::parse(owner, txt);
        auto prop = path.getProperty();
        if (prop && !App::ObjectIdentifier::isPseudoProperty(prop)) {
            auto obj = path.getDocumentObject();
            if (obj) {
                out.obj = App::SubObjectT(obj, path.getSubObjectName().c_str());
                out.prop = prop;
                out.path = std::move(path);
                return true;
            }
        }
    }
    catch (Base::Exception &) {
    }
    catch (...) {
    }

    // Else an object path, resolved the way the tree's search box does
    // it: '_self' is a pseudo property every object answers to, so a
    // text that parses to it named an object.
    try {
        std::string objText = txt;
        if (objText.back() != '.')
            objText += '.';
        objText += "_self";
        auto path = App::ObjectIdentifier::parse(owner, objText);
        if (path.getPropertyName() != "_self")
            return false;
        auto obj = path.getDocumentObject();
        if (!obj)
            return false;
        out.obj = App::SubObjectT(obj, path.getSubObjectName().c_str());
        out.prop = nullptr;
        out.path = std::move(path);
        return true;
    }
    catch (Base::Exception &) {
    }
    catch (...) {
    }
    return false;
}

// ---------------------------------------------------------------------------
// commands

std::vector<OmniSearch::CommandMatch> OmniSearch::searchCommands(const QString &query)
{
    std::vector<CommandMatch> res;
    auto keys = ParamRegistry::splitKeywords(query.toUtf8().constData());
    CommandListModel model;
    for (int row = 0, count = model.rowCount(); row < count; ++row) {
        auto index = model.index(row, 0);
        std::string text = index.data(CommandListModel::SearchTextRole).toString().toUtf8().constData();
        if (!ParamRegistry::matchKeywords(text, keys))
            continue;
        CommandMatch match;
        match.name = index.data(CommandListModel::CommandNameRole).toByteArray();
        match.title = index.data(CommandListModel::TitleRole).toString();
        match.description = index.data(CommandListModel::DescriptionRole).toString();
        match.shortcut = index.data(CommandListModel::ShortcutRole).toString();
        match.group = index.data(CommandListModel::IsGroupRole).toBool();
        match.active = index.data(CommandListModel::IsActiveRole).toBool();
        res.push_back(std::move(match));
    }
    return res;
}

// ---------------------------------------------------------------------------
// parameters

std::vector<OmniSearch::ParamMatch> OmniSearch::searchParams(const QString &query)
{
    std::vector<ParamMatch> res;
    auto &reg = ParamRegistry::instance();
    for (auto info : reg.search(ParamRegistry::splitKeywords(query.toUtf8().constData()))) {
        ParamMatch match;
        match.info = info;
        match.value = reg.getValue(*info);
        res.push_back(std::move(match));
    }
    return res;
}

static QString translateParamText(const ParamInfo &info, const char *text)
{
    if (!text || !text[0])
        return {};
    return QCoreApplication::translate(info.className, text);
}

static QWidget *createParamEditorByType(const ParamInfo &info, QWidget *parent)
{
    switch (info.type) {
    case ParamInfo::Bool: {
        auto w = new PrefCheckBox(parent);
        w->setText(translateParamText(info, info.title));
        return w;
    }
    case ParamInfo::Int: {
        auto w = new PrefSpinBox(parent);
        w->setRange(INT_MIN, INT_MAX);
        return w;
    }
    case ParamInfo::UInt: {
        auto w = new PrefSpinBox(parent);
        w->setRange(0, INT_MAX);
        return w;
    }
    case ParamInfo::Hex: {
        // Every Hex parameter in the tree is a packed colour
        auto w = new PrefColorButton(parent);
        w->setAllowTransparency(true);
        return w;
    }
    case ParamInfo::Float: {
        auto w = new PrefDoubleSpinBox(parent);
        w->setRange(-1e9, 1e9);
        w->setDecimals(6);
        return w;
    }
    case ParamInfo::String:
        return new PrefLineEdit(parent);
    }
    return nullptr;
}

void OmniSearch::setParamEditorValue(QWidget *widget, const ParamInfo &info, const std::string &value)
{
    if (!widget)
        return;
    const char *text = value.c_str();
    if (auto w = qobject_cast<PrefLinePattern*>(widget)) {
        long v = std::strtol(text, nullptr, 0);
        for (int i = 0; i < w->count(); ++i) {
            if (w->itemData(i).toInt() == v) {
                w->setCurrentIndex(i);
                break;
            }
        }
    }
    else if (auto w = qobject_cast<PrefComboBox*>(widget)) {
        if (info.comboDataIsString)
            w->setCurrentIndex(w->findData(QByteArray(text)));
        else
            w->setCurrentIndex(static_cast<int>(std::strtol(text, nullptr, 0)));
    }
    else if (auto w = qobject_cast<PrefColorButton*>(widget)) {
        w->setPackedColor(static_cast<uint32_t>(std::strtoul(text, nullptr, 0)));
    }
    else if (auto w = qobject_cast<PrefFileChooser*>(widget)) {
        w->setFileNameStd(value);
    }
    else if (auto w = qobject_cast<PrefAccelLineEdit*>(widget)) {
        w->setDisplayText(value);
    }
    else if (auto w = qobject_cast<PrefCheckBox*>(widget)) {
        w->setChecked(value == "true");
    }
    else if (auto w = qobject_cast<PrefSpinBox*>(widget)) {
        w->setValue(static_cast<int>(std::strtol(text, nullptr, 0)));
    }
    else if (auto w = qobject_cast<PrefDoubleSpinBox*>(widget)) {
        w->setValue(std::strtod(text, nullptr));
    }
    else if (auto w = qobject_cast<PrefLineEdit*>(widget)) {
        w->setText(QString::fromUtf8(text));
    }
}

QWidget *OmniSearch::createParamEditor(const ParamInfo &info, QWidget *parent)
{
    QWidget *widget = nullptr;
    const char *proxy = info.proxy ? info.proxy : "";

    if (std::strcmp(proxy, "ComboBox") == 0) {
        auto w = new PrefComboBox(parent);
        if (info.comboDataIsString)
            w->setProperty("prefType", QByteArray());
        for (const auto &item : info.items) {
            QString text = info.translateItems
                ? translateParamText(info, item.text)
                : QString::fromUtf8(item.text);
            if (item.data)
                w->addItem(text, QByteArray(item.data));
            else
                w->addItem(text);
            if (item.tooltip && item.tooltip[0])
                w->setItemData(w->count() - 1, translateParamText(info, item.tooltip), Qt::ToolTipRole);
        }
        widget = w;
    }
    else if (std::strcmp(proxy, "LinePattern") == 0) {
        widget = new PrefLinePattern(parent);
    }
    else if (std::strcmp(proxy, "Color") == 0) {
        auto w = new PrefColorButton(parent);
        w->setAllowTransparency(info.transparency);
        widget = w;
    }
    else if (std::strcmp(proxy, "File") == 0) {
        widget = new PrefFileChooser(parent);
    }
    else if (std::strcmp(proxy, "ShortcutEdit") == 0) {
        widget = new PrefAccelLineEdit(parent);
    }
    else if (std::strcmp(proxy, "SpinBox") == 0) {
        if (info.type == ParamInfo::Float || info.decimals > 0) {
            auto w = new PrefDoubleSpinBox(parent);
            w->setRange(info.minimum, info.maximum);
            w->setSingleStep(info.step);
            w->setDecimals(info.decimals);
            widget = w;
        }
        else {
            auto w = new PrefSpinBox(parent);
            w->setRange(static_cast<int>(info.minimum), static_cast<int>(info.maximum));
            w->setSingleStep(static_cast<int>(info.step));
            widget = w;
        }
    }

    if (!widget)
        widget = createParamEditorByType(info, parent);
    if (!widget)
        return nullptr;

    // A preference widget restores with its current value as the fallback
    // for an unset entry, so seed it with the effective value first -- the
    // generated pages seed the default the same way before binding.
    setParamEditorValue(widget, info, ParamRegistry::instance().getValue(info));
    auto pref = dynamic_cast<PrefWidget*>(widget);
    if (pref) {
        pref->setEntryName(QByteArray(info.entry));
        pref->setParamGrpPath(QByteArray(info.path));
        pref->onRestore();
    }
    widget->setToolTip(translateParamText(info, info.doc));
    return widget;
}

// ---------------------------------------------------------------------------
// ParamListModel

ParamListModel::ParamListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    entries = ParamRegistry::instance().entries();
}

void ParamListModel::refresh()
{
    beginResetModel();
    entries = ParamRegistry::instance().entries();
    endResetModel();
}

const ParamInfo *ParamListModel::info(const QModelIndex &index) const
{
    if (index.row() < 0 || index.row() >= (int)entries.size())
        return nullptr;
    return entries[index.row()];
}

const ParamInfo *ParamListModel::infoOf(const QModelIndex &index)
{
    auto ptr = index.data(OmniSearch::ParamInfoRole).value<quintptr>();
    return reinterpret_cast<const ParamInfo*>(ptr);
}

static QString firstLine(const QString &text)
{
    int pos = text.indexOf(QLatin1Char('\n'));
    return pos < 0 ? text : text.left(pos);
}

QVariant ParamListModel::data(const QModelIndex &index, int role) const
{
    auto info = this->info(index);
    if (!info)
        return {};

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case OmniSearch::TitleRole:
        return QString::fromUtf8(info->fullName().c_str());

    case OmniSearch::DescriptionRole: {
        QString doc = firstLine(translateParamText(*info, info->doc));
        QString title = translateParamText(*info, info->title);
        if (doc.isEmpty())
            return title;
        if (title.isEmpty() || title == doc)
            return doc;
        return QStringLiteral("%1 -- %2").arg(title, doc);
    }

    case Qt::ToolTipRole: {
        QString tip = QString::fromUtf8(info->fullPath().c_str());
        QString doc = translateParamText(*info, info->doc);
        if (!doc.isEmpty())
            tip += QStringLiteral("\n\n") + doc;
        return tip;
    }

    case OmniSearch::ShortcutRole:
    case OmniSearch::ParamValueRole:
        return QString::fromUtf8(ParamRegistry::instance().getValue(*info).c_str());

    case OmniSearch::ParamPathRole:
        return QString::fromUtf8(info->fullPath().c_str());

    case OmniSearch::IsGroupRole:
        return false;

    case OmniSearch::IsActiveRole:
        return true;

    case OmniSearch::SearchTextRole:
        return QString::fromUtf8(info->searchText().c_str());

    case OmniSearch::ParamInfoRole:
        return QVariant::fromValue(reinterpret_cast<quintptr>(info));

    default:
        break;
    }
    return {};
}

int ParamListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return (int)entries.size();
}

// ---------------------------------------------------------------------------
// KeywordFilterModel

KeywordFilterModel::KeywordFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void KeywordFilterModel::setKeywords(const QString &query)
{
    auto newKeys = ParamRegistry::splitKeywords(query.toUtf8().constData());
    if (newKeys == keys)
        return;
    keys = std::move(newKeys);
    invalidateFilter();
}

bool KeywordFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (keys.empty())
        return true;
    auto index = sourceModel()->index(sourceRow, 0, sourceParent);
    std::string text = index.data(searchRole).toString().toUtf8().constData();
    return ParamRegistry::matchKeywords(text, keys);
}

#include "moc_OmniSearch.cpp"
