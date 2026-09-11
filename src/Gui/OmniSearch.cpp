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
# include <cctype>
# include <cstdlib>
# include <cstring>
# include <QCoreApplication>
#endif

#include <boost/algorithm/string/predicate.hpp>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ParamRegistry.h>
#include <App/Property.h>
#include <App/PropertyContainer.h>
#include <Base/Exception.h>
#include <Base/Type.h>

#include "OmniSearch.h"
#include "Application.h"
#include "Command.h"
#include "CommandCompleter.h"
#include "Document.h"
#include "MDIView.h"
#include "PrefWidgets.h"
#include "ViewProvider.h"

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

namespace {

// The first '#' outside a <<...>> string: the document separator
int documentSeparator(const std::string &txt)
{
    bool quoted = false;
    for (size_t i = 0; i < txt.size(); ++i) {
        if (txt.compare(i, 2, "<<") == 0) {
            quoted = true;
            ++i;
        }
        else if (txt.compare(i, 2, ">>") == 0) {
            quoted = false;
            ++i;
        }
        else if (!quoted && txt[i] == '#')
            return static_cast<int>(i);
    }
    return -1;
}

// The document "Doc" or "<<Label>>" names; the owner's for an empty name.
// An unquoted name is tried as a name first, then as a label, as
// ObjectIdentifier does; an ambiguous label names nothing.
App::Document *findDocument(const std::string &name, App::DocumentObject *owner)
{
    if (name.empty())
        return owner->getDocument();
    std::string label = name;
    if (name.size() >= 4 && boost::starts_with(name, "<<") && boost::ends_with(name, ">>"))
        label = name.substr(2, name.size() - 4);
    else if (auto doc = App::GetApplication().getDocument(name.c_str()))
        return doc;
    App::Document *found = nullptr;
    for (auto doc : App::GetApplication().getDocuments()) {
        if (doc->Label.getStrValue() != label)
            continue;
        if (found)
            return nullptr;
        found = doc;
    }
    return found;
}

bool isIdentifier(const std::string &s)
{
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
        return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    }
    return true;
}

// The document's active 3D view, a property container of its own. The
// active MDI view may be a ViewArea hosting it; activeSubView() is the
// view inside.
App::PropertyContainer *activeView(App::Document *doc)
{
    if (!Gui::Application::Instance)
        return nullptr;
    auto gdoc = Gui::Application::Instance->getDocument(doc);
    auto view = gdoc ? gdoc->getActiveView() : nullptr;
    return view ? view->activeSubView() : nullptr;
}

// "Comment" or "ActiveView.DrawStyle" after "#.": a property of the
// document itself, or of its active 3D view
bool resolveDocumentMember(App::Document *doc, const std::string &member, OmniSearch::ObjectMatch &out)
{
    App::PropertyContainer *container = doc;
    std::string name = member;
    auto dot = member.find('.');
    if (dot != std::string::npos) {
        if (member.compare(0, dot, "ActiveView") != 0)
            return false;
        name = member.substr(dot + 1);
        container = activeView(doc);
        if (!container)
            return false;
    }
    if (!isIdentifier(name))
        return false;
    auto prop = container->getPropertyByName(name.c_str());
    if (!prop)
        return false;
    out.doc = doc;
    out.prop = prop;
    out.props.push_back(prop);
    return true;
}

// The property a path names on its owner, with the (sub-)object it
// belongs to. A pseudo property is no match, except ViewObject followed
// by one name: the property of the object's view provider.
App::Property *pathProperty(App::DocumentObject *owner, const std::string &txt,
                            App::SubObjectT &objT, App::ObjectIdentifier &pathOut)
{
    try {
        auto path = App::ObjectIdentifier::parse(owner, txt);
        int ptype = 0;  // non-zero: a pseudo property, which resolves to a stand-in
        auto prop = path.getProperty(&ptype);
        auto obj = prop ? path.getDocumentObject() : nullptr;
        if (!obj)
            return nullptr;
        const auto &sub = path.getSubObjectName();
        if (ptype) {
            if (path.getPropertyName() != "ViewObject" || path.numSubComponents() != 2)
                return nullptr;
            const auto &comp = path.getPropertyComponent(1);
            if (!comp.isSimple())
                return nullptr;
            auto target = sub.empty() ? obj : obj->getSubObject(sub.c_str());
            auto vp = (target && Gui::Application::Instance)
                ? Gui::Application::Instance->getViewProvider(target) : nullptr;
            prop = vp ? vp->getPropertyByName(comp.getName().c_str()) : nullptr;
            if (!prop)
                return nullptr;
        }
        objT = App::SubObjectT(obj, sub.c_str());
        pathOut = std::move(path);
        return prop;
    }
    catch (Base::Exception &) {
    }
    catch (...) {
    }
    return nullptr;
}

} // namespace

bool OmniSearch::resolveObject(const QString &query, App::DocumentObject *owner, ObjectMatch &out,
                               const std::vector<App::DocumentObject*> *locals)
{
    out = ObjectMatch();
    if (!owner || !owner->isAttachedToDocument())
        return false;
    std::string txt = query.trimmed().toUtf8().constData();
    if (txt.empty())
        return false;

    // '#' addresses a document: "#.Comment" and "Doc#.Comment" a member
    // of the document itself, "#Box" an object of the owner's document
    // (the expression grammar only knows "Doc#Box").
    int sep = documentSeparator(txt);
    if (sep >= 0 && sep + 1 < static_cast<int>(txt.size()) && txt[sep + 1] == '.') {
        auto doc = findDocument(txt.substr(0, sep), owner);
        return doc && resolveDocumentMember(doc, txt.substr(sep + 2), out);
    }
    if (sep == 0) {
        txt.erase(0, 1);
        if (txt.empty())
            return false;
    }

    bool local = txt[0] == '.';
    if (local && locals && locals->empty())
        return false;

    // A property reference first: "Box.Length", "Part.Box.Placement",
    // "Box.ViewObject.ShapeColor".
    if (auto prop = pathProperty(owner, txt, out.obj, out.path)) {
        out.prop = prop;
        out.props.push_back(prop);
        if (local && locals) {
            auto obj = out.obj.getObject();
            for (auto other : *locals) {
                if (other == obj || !other || !other->isAttachedToDocument())
                    continue;
                App::SubObjectT t;
                App::ObjectIdentifier p;
                if (auto pp = pathProperty(other, txt, t, p)) {
                    if (t.getObject() == other && pp->getTypeId() == prop->getTypeId())
                        out.props.push_back(pp);
                }
            }
        }
        return true;
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

std::vector<OmniSearch::MemberMatch> OmniSearch::documentMembers(const QString &head,
                                                                 App::DocumentObject *owner)
{
    std::vector<MemberMatch> res;
    if (!owner || !owner->isAttachedToDocument())
        return res;
    std::string txt = head.toUtf8().constData();
    int sep = documentSeparator(txt);
    if (sep < 0 || sep + 1 >= static_cast<int>(txt.size()) || txt[sep + 1] != '.')
        return res;
    auto doc = findDocument(txt.substr(0, sep), owner);
    if (!doc)
        return res;
    App::PropertyContainer *container = doc;
    std::string rest = txt.substr(sep + 2);
    if (rest == "ActiveView.") {
        container = activeView(doc);
        if (!container)
            return res;
    }
    else if (!rest.empty())
        return res;

    std::vector<std::pair<const char*, App::Property*>> props;
    container->getPropertyNamedList(props);
    for (auto &v : props) {
        if (!v.second || v.second->testStatus(App::Property::Hidden))
            continue;
        MemberMatch m;
        m.name = QString::fromUtf8(v.first);
        const char *docu = v.second->getDocumentation();
        m.description = QString::fromUtf8(docu ? docu : "");
        res.push_back(std::move(m));
    }
    if (container == doc && activeView(doc)) {
        MemberMatch m;
        m.name = QStringLiteral("ActiveView.");
        m.description = QCoreApplication::translate("Gui::OmniSearch", "The active 3D view's properties");
        res.push_back(std::move(m));
    }
    return res;
}

bool OmniSearch::splitMemberQuery(const QString &query, QString &head, QString &tail)
{
    // The '#' outside a <<...>> string, as documentSeparator() finds it
    int sep = -1;
    bool quoted = false;
    for (int i = 0; i < query.size(); ++i) {
        if (query.mid(i, 2) == QLatin1String("<<")) {
            quoted = true;
            ++i;
        }
        else if (query.mid(i, 2) == QLatin1String(">>")) {
            quoted = false;
            ++i;
        }
        else if (!quoted && query[i] == QLatin1Char('#')) {
            sep = i;
            break;
        }
    }
    if (sep < 0 || sep + 1 >= query.size() || query[sep + 1] != QLatin1Char('.'))
        return false;
    int start = sep + 2;
    static const QString view = QStringLiteral("ActiveView.");
    if (query.mid(start).startsWith(view))
        start += view.size();
    tail = query.mid(start);
    if (tail.contains(QLatin1Char('.')))
        return false;
    head = query.left(start);
    return true;
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
        return QString::fromUtf8(info->displayPath().c_str());

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
