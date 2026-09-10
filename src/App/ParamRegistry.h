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

#ifndef APP_PARAM_REGISTRY_H
#define APP_PARAM_REGISTRY_H

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <FCGlobal.h>

namespace App
{

/** Static description of one generated application parameter.
 *
 * Every parameter declared through Tools/params_utils.py (ViewParams,
 * TreeParams, DocumentParams, ...) is described by one of these, registered
 * at library load by a block the generator emits at the bottom of each
 * XxxParams.cpp. The description is what a parameter search needs -- where
 * the value lives, what type it has, its documentation -- plus the widget
 * hints the preference page was generated from (the "proxy"), so that an
 * editor can be built for the parameter outside that page.
 *
 * All strings are literals owned by the generated code.
 */
struct AppExport ParamInfo
{
    enum Type
    {
        Bool,
        Int,
        UInt,
        Hex,  // unsigned, conventionally a packed RGBA colour
        Float,
        String,
    };

    struct Item
    {
        const char* text;
        const char* tooltip;
        const char* data;  // stored value when comboDataIsString, else null
    };

    /** The default as the generated code spells it. A parameter's default
     * is a C++ expression -- a literal, or a macro such as
     * FC_EXPR_PARAM_EDIT_BG_ALPHA -- so it arrives as a value, not text,
     * and is formatted by the constructor according to the type.
     */
    struct AppExport Default
    {
        Default(bool v)
            : number(v ? 1 : 0)
        {}
        Default(int v)
            : number(v)
        {}
        Default(unsigned v)
            : number(v)
        {}
        Default(long v)
            : number(v)
        {}
        Default(unsigned long v)
            : number(v)
        {}
        Default(long long v)
            : number(v)
        {}
        Default(unsigned long long v)
            : number(static_cast<long double>(v))
        {}
        Default(double v)
            : number(v)
        {}
        Default(const char* v)
            : text(v)
            , isText(true)
        {}
        long double number = 0;
        const char* text = "";
        bool isText = false;
    };

    ParamInfo(const char* nameSpace,
              const char* className,
              const char* path,
              const char* name,
              const char* entry,
              Type type,
              const Default& defaultValue);

    // identity
    const char* nameSpace;  // C++ namespace of the accessor class, "Gui"
    const char* className;  // accessor class, "ViewParams"; also the translation context
    const char* path;       // full group path, e.g. "User parameter:BaseApp/Preferences/View"
    const char* name;       // accessor suffix, getXxx()
    const char* entry;      // stored entry name, usually the same as name
    Type type;
    std::string defaultValue;  // textual, in the form getValue() returns

    const char* title = "";
    const char* doc = "";  // untranslated; translate with className as context
    bool onChange = false;

    // proxy: the preference-page widget the parameter was generated with.
    // "" for none, else "ComboBox", "LinePattern", "Color", "File",
    // "SpinBox", "ShortcutEdit", or the name of a custom proxy class.
    const char* proxy = "";
    double minimum = 0.0;  // SpinBox
    double maximum = 0.0;
    double step = 0.0;
    int decimals = 0;
    bool transparency = false;  // Color
    bool comboDataIsString = false;  // ComboBox: the stored value is Item::data, not the index
    bool translateItems = true;
    std::vector<Item> items;

    // builder, used by the generated code
    ParamInfo& setTitle(const char* v)
    {
        title = v;
        return *this;
    }
    ParamInfo& setDoc(const char* v)
    {
        doc = v;
        return *this;
    }
    ParamInfo& setOnChange(bool v = true)
    {
        onChange = v;
        return *this;
    }
    ParamInfo& setProxy(const char* v)
    {
        proxy = v;
        return *this;
    }
    ParamInfo& setRange(double min, double max, double stepValue, int dec = 0)
    {
        minimum = min;
        maximum = max;
        step = stepValue;
        decimals = dec;
        return *this;
    }
    ParamInfo& setTransparency(bool v)
    {
        transparency = v;
        return *this;
    }
    ParamInfo& setItems(std::vector<Item> v, bool dataIsString = false, bool translate = true)
    {
        items = std::move(v);
        comboDataIsString = dataIsString;
        translateItems = translate;
        return *this;
    }

    /// "Gui::ViewParams::UseViewArea"
    std::string fullName() const;
    /// path + "/" + entry
    std::string fullPath() const;
    /// The text a keyword search runs over: fullPath, fullName, title and doc.
    std::string searchText() const;
};

/** The registry of every generated parameter in the loaded libraries.
 *
 * Values are read and written through the ParameterGrp so that the
 * generated observer classes, which cache every value, see the change.
 */
class AppExport ParamRegistry
{
public:
    static ParamRegistry& instance();

    void add(std::vector<ParamInfo>&& infos);

    const std::vector<const ParamInfo*>& entries() const
    {
        return _entries;
    }

    /// Look up by group path and entry name; null when unknown.
    const ParamInfo* find(const char* path, const char* entry) const;

    /** Every entry whose searchText() contains all of the keywords,
     * case-insensitively, in registration order. No keywords: everything.
     */
    std::vector<const ParamInfo*> search(const std::vector<std::string>& keywords) const;

    /// Split a query on whitespace into keywords.
    static std::vector<std::string> splitKeywords(const std::string& query);

    /// True when every keyword is a case-insensitive substring of haystack.
    static bool matchKeywords(const std::string& haystack, const std::vector<std::string>& keywords);

    /// The current value as text: true/false, decimal, 0x-hex for Hex, %.15g, or the string.
    std::string getValue(const ParamInfo& info) const;

    /// Parse text in the same form and store it. False when it does not parse.
    bool setValue(const ParamInfo& info, const std::string& value) const;

    /// Remove the stored entry so the parameter reads its default again.
    void reset(const ParamInfo& info) const;

    /// Whether the entry is stored, i.e. differs from an unset default.
    bool isSet(const ParamInfo& info) const;

    /// One static instance of this per generated XxxParams.cpp.
    struct AppExport Registrar
    {
        explicit Registrar(std::vector<ParamInfo>&& infos);
    };

private:
    ParamRegistry() = default;
    static std::string key(const char* path, const char* entry);

    std::deque<ParamInfo> _infos;  // stable addresses
    std::vector<const ParamInfo*> _entries;
    std::unordered_map<std::string, const ParamInfo*> _index;
};

}  // namespace App

#endif  // APP_PARAM_REGISTRY_H
