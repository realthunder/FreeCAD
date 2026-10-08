/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#endif

#include <Base/Parameter.h>

#include "ParamRegistry.h"
#include "Application.h"

using namespace App;

ParamInfo::ParamInfo(const char* nameSpace_,
                     const char* className_,
                     const char* path_,
                     const char* name_,
                     const char* entry_,
                     Type type_,
                     const Default& def)
    : nameSpace(nameSpace_)
    , className(className_)
    , path(path_)
    , name(name_)
    , entry(entry_)
    , type(type_)
{
    char buf[64];
    switch (type) {
        case Bool:
            defaultValue = def.isText ? def.text : (def.number != 0 ? "true" : "false");
            return;
        case Int:
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(def.number));
            break;
        case UInt:
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(def.number));
            break;
        case Hex:
            std::snprintf(buf, sizeof(buf), "0x%08llX", static_cast<unsigned long long>(def.number));
            break;
        case Float:
            std::snprintf(buf, sizeof(buf), "%.15g", static_cast<double>(def.number));
            break;
        case String:
            defaultValue = def.text;
            return;
    }
    defaultValue = def.isText ? def.text : buf;
}

std::string ParamInfo::fullName() const
{
    std::string res(nameSpace);
    res += "::";
    res += className;
    res += "::";
    res += name;
    return res;
}

std::string ParamInfo::fullPath() const
{
    std::string res(path);
    res += '/';
    res += entry;
    return res;
}

std::string ParamInfo::displayPath() const
{
    const char* p = path ? path : "";
    for (const char* prefix : {"User parameter:", "System parameter:"}) {
        auto len = std::strlen(prefix);
        if (std::strncmp(p, prefix, len) == 0) {
            p += len;
            break;
        }
    }
    while (*p == '/') {
        ++p;
    }
    // With the root dropped there is nothing for a leading '/' to be
    // relative to, so it goes too; a path not under BaseApp keeps it.
    std::string res;
    if (std::strncmp(p, "BaseApp", 7) == 0 && (p[7] == '/' || p[7] == '\0')) {
        p += 7;
        while (*p == '/') {
            ++p;
        }
    }
    else {
        res = "/";
    }
    res += p;
    if (!res.empty() && res.back() != '/') {
        res += '/';
    }
    res += entry;
    return res;
}

std::string ParamInfo::searchText() const
{
    std::string res = fullPath();
    res += ' ';
    res += fullName();
    res += ' ';
    res += title;
    res += ' ';
    res += doc;
    return res;
}

// ---------------------------------------------------------------------------

ParamRegistry& ParamRegistry::instance()
{
    static ParamRegistry* inst = new ParamRegistry;
    return *inst;
}

std::string ParamRegistry::key(const char* path, const char* entry)
{
    std::string res(path);
    res += '/';
    res += entry;
    return res;
}

const ParamInfo* ParamRegistry::insert(ParamInfo&& info)
{
    _infos.push_back(std::move(info));
    const ParamInfo* ptr = &_infos.back();
    _entries.push_back(ptr);
    _index[key(ptr->path, ptr->entry)] = ptr;
    return ptr;
}

void ParamRegistry::add(std::vector<ParamInfo>&& infos)
{
    for (auto& info : infos) {
        insert(std::move(info));
    }
}

const char* ParamRegistry::keep(const std::string& text)
{
    if (text.empty()) {
        return "";
    }
    _strings.push_back(text);
    return _strings.back().c_str();
}

const ParamInfo* ParamRegistry::add(const ParamSpec& spec)
{
    std::string def;
    if (spec.path.empty() || spec.entry.empty()
        || !normalizeValue(spec.type, spec.defaultValue, def)
        || find(spec.path.c_str(), spec.entry.c_str())) {
        return nullptr;
    }
    // the constructor copies the default, the one string an entry owns
    ParamInfo info(keep(spec.nameSpace),
                   keep(spec.className),
                   keep(spec.path),
                   keep(spec.name.empty() ? spec.entry : spec.name),
                   keep(spec.entry),
                   spec.type,
                   ParamInfo::Default(def.c_str()));
    info.setTitle(keep(spec.title))
        .setDoc(keep(spec.doc))
        .setOnChange(spec.onChange)
        .setProxy(keep(spec.proxy))
        .setRange(spec.minimum, spec.maximum, spec.step, spec.decimals)
        .setTransparency(spec.transparency);
    std::vector<ParamInfo::Item> items;
    items.reserve(spec.items.size());
    for (const auto& item : spec.items) {
        items.push_back({keep(item.text),
                         keep(item.tooltip),
                         spec.comboDataIsString ? keep(item.data) : nullptr});
    }
    info.setItems(std::move(items), spec.comboDataIsString, spec.translateItems);
    return insert(std::move(info));
}

const ParamInfo* ParamRegistry::find(const char* path, const char* entry) const
{
    if (!path || !entry) {
        return nullptr;
    }
    auto it = _index.find(key(path, entry));
    return it == _index.end() ? nullptr : it->second;
}

std::vector<std::string> ParamRegistry::splitKeywords(const std::string& query)
{
    std::vector<std::string> res;
    std::istringstream iss(query);
    std::string word;
    while (iss >> word) {
        res.push_back(word);
    }
    return res;
}

static std::string toLower(const std::string& s)
{
    std::string res(s);
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return res;
}

bool ParamRegistry::matchKeywords(const std::string& haystack, const std::vector<std::string>& keywords)
{
    if (keywords.empty()) {
        return true;
    }
    std::string hay = toLower(haystack);
    for (const auto& kw : keywords) {
        if (kw.empty()) {
            continue;
        }
        if (hay.find(toLower(kw)) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::vector<const ParamInfo*> ParamRegistry::search(const std::vector<std::string>& keywords) const
{
    std::vector<const ParamInfo*> res;
    for (auto info : _entries) {
        if (matchKeywords(info->searchText(), keywords)) {
            res.push_back(info);
        }
    }
    return res;
}

static ParameterGrp::handle groupOf(const ParamInfo& info)
{
    return App::GetApplication().GetParameterGroupByPath(info.path);
}

namespace
{

// A value read from text, by type: what setValue() stores and
// normalizeValue() spells out again.
struct ParsedValue
{
    bool boolean = false;
    long integer = 0;
    unsigned long natural = 0;
    double real = 0.0;
};

bool parseValue(ParamInfo::Type type, const std::string& value, ParsedValue& res)
{
    const char* text = value.c_str();
    char* end = nullptr;
    errno = 0;
    switch (type) {
        case ParamInfo::Bool: {
            std::string v = toLower(value);
            if (v == "true" || v == "1" || v == "yes" || v == "on") {
                res.boolean = true;
            }
            else if (v == "false" || v == "0" || v == "no" || v == "off") {
                res.boolean = false;
            }
            else {
                return false;
            }
            return true;
        }
        case ParamInfo::Int:
            res.integer = std::strtol(text, &end, 0);
            break;
        case ParamInfo::UInt:
        case ParamInfo::Hex:
            res.natural = std::strtoul(text, &end, 0);
            break;
        case ParamInfo::Float:
            res.real = std::strtod(text, &end);
            break;
        case ParamInfo::String:
            return true;
    }
    return end != text && !*end && !errno;
}

}  // namespace

std::string ParamRegistry::getValue(const ParamInfo& info) const
{
    auto hGrp = groupOf(info);
    char buf[64];
    switch (info.type) {
        case ParamInfo::Bool: {
            bool def = info.defaultValue == "true";
            return hGrp->GetBool(info.entry, def) ? "true" : "false";
        }
        case ParamInfo::Int: {
            long def = std::strtol(info.defaultValue.c_str(), nullptr, 0);
            std::snprintf(buf, sizeof(buf), "%ld", hGrp->GetInt(info.entry, def));
            return buf;
        }
        case ParamInfo::UInt: {
            unsigned long def = std::strtoul(info.defaultValue.c_str(), nullptr, 0);
            std::snprintf(buf, sizeof(buf), "%lu", hGrp->GetUnsigned(info.entry, def));
            return buf;
        }
        case ParamInfo::Hex: {
            unsigned long def = std::strtoul(info.defaultValue.c_str(), nullptr, 0);
            std::snprintf(buf, sizeof(buf), "0x%08lX", hGrp->GetUnsigned(info.entry, def));
            return buf;
        }
        case ParamInfo::Float: {
            double def = std::strtod(info.defaultValue.c_str(), nullptr);
            std::snprintf(buf, sizeof(buf), "%.15g", hGrp->GetFloat(info.entry, def));
            return buf;
        }
        case ParamInfo::String:
            return hGrp->GetASCII(info.entry, info.defaultValue.c_str());
    }
    return {};
}

bool ParamRegistry::setValue(const ParamInfo& info, const std::string& value) const
{
    ParsedValue v;
    if (!parseValue(info.type, value, v)) {
        return false;
    }
    auto hGrp = groupOf(info);
    switch (info.type) {
        case ParamInfo::Bool:
            hGrp->SetBool(info.entry, v.boolean);
            break;
        case ParamInfo::Int:
            hGrp->SetInt(info.entry, v.integer);
            break;
        case ParamInfo::UInt:
        case ParamInfo::Hex:
            hGrp->SetUnsigned(info.entry, v.natural);
            break;
        case ParamInfo::Float:
            hGrp->SetFloat(info.entry, v.real);
            break;
        case ParamInfo::String:
            hGrp->SetASCII(info.entry, value);
            break;
    }
    return true;
}

bool ParamRegistry::normalizeValue(ParamInfo::Type type, const std::string& value, std::string& res)
{
    ParsedValue v;
    if (!parseValue(type, value, v)) {
        return false;
    }
    char buf[64];
    switch (type) {
        case ParamInfo::Bool:
            res = v.boolean ? "true" : "false";
            return true;
        case ParamInfo::Int:
            std::snprintf(buf, sizeof(buf), "%ld", v.integer);
            break;
        case ParamInfo::UInt:
            std::snprintf(buf, sizeof(buf), "%lu", v.natural);
            break;
        case ParamInfo::Hex:
            std::snprintf(buf, sizeof(buf), "0x%08lX", v.natural);
            break;
        case ParamInfo::Float:
            std::snprintf(buf, sizeof(buf), "%.15g", v.real);
            break;
        case ParamInfo::String:
            res = value;
            return true;
    }
    res = buf;
    return true;
}

void ParamRegistry::reset(const ParamInfo& info) const
{
    auto hGrp = groupOf(info);
    switch (info.type) {
        case ParamInfo::Bool:
            hGrp->RemoveBool(info.entry);
            break;
        case ParamInfo::Int:
            hGrp->RemoveInt(info.entry);
            break;
        case ParamInfo::UInt:
        case ParamInfo::Hex:
            hGrp->RemoveUnsigned(info.entry);
            break;
        case ParamInfo::Float:
            hGrp->RemoveFloat(info.entry);
            break;
        case ParamInfo::String:
            hGrp->RemoveASCII(info.entry);
            break;
    }
}

bool ParamRegistry::isSet(const ParamInfo& info) const
{
    auto hGrp = groupOf(info);
    for (const auto& v : hGrp->GetParameterNames(info.entry)) {
        if (v.second == info.entry) {
            return true;
        }
    }
    return false;
}

ParamRegistry::Registrar::Registrar(std::vector<ParamInfo>&& infos)
{
    ParamRegistry::instance().add(std::move(infos));
}
