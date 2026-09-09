/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
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
#include <cassert>
#endif
#include <filesystem>
#include <system_error>

/// Here the FreeCAD includes sorted by Base,App,Gui......
#include "Type.h"
#include "Exception.h"
#include "Interpreter.h"
#include "Console.h"


using namespace Base;
using namespace std;


struct Base::TypeData
{
    TypeData(const char* theName,
             const Type type = Type::badType(),
             const Type theParent = Type::badType(),
             Type::instantiationMethod method = nullptr)
        : name(theName)
        , parent(theParent)
        , type(type)
        , instMethod(method)
    {}

    std::string name;
    Type parent;
    Type type;
    Type::instantiationMethod instMethod;
};

map<string, unsigned int> Type::typemap;
vector<TypeData*> Type::typedata;
set<string> Type::loadModuleSet;

void* Type::createInstance()
{
    instantiationMethod method = typedata[index]->instMethod;
    return method ? (*method)() : nullptr;
}


void* Type::createInstanceByName(const char* TypeName, bool bLoadModule)
{
    // if not already, load the module
    if (bLoadModule) {
        importModule(TypeName);
    }

    // now the type should be in the type map
    Type type = fromName(TypeName);
    if (type == badType()) {
        return nullptr;
    }

    return type.createInstance();
}

void Type::importModule(const char* TypeName)
{
    // cut out the module name
    string Mod = getModuleName(TypeName);
    // ignore base modules
    if (Mod != "App" && Mod != "Gui" && Mod != "Base") {
        // remember already loaded modules
        set<string>::const_iterator pos = loadModuleSet.find(Mod);
        if (pos == loadModuleSet.end()) {
            if (!moduleAllowed(Mod)) {
                throw RuntimeError("type '" + string(TypeName) + "' names module '" + Mod
                                   + "', which is not a FreeCAD module (not loaded and not "
                                     "under a Mod directory); not imported");
            }
            Interpreter().loadModule(Mod.c_str());
#ifdef FC_LOGLOADMODULE
            Console().Log("Act: Module %s loaded through class %s \n", Mod.c_str(), TypeName);
#endif
            loadModuleSet.insert(Mod);
        }
    }
}

vector<string> Type::moduleRoots;

void Type::addModuleRoot(const std::string& dir)
{
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        p = std::filesystem::path(dir).lexically_normal();
    }
    string s = p.generic_string();
    while (s.size() > 1 && s.back() == '/') {
        s.pop_back();
    }
    for (const auto& r : moduleRoots) {
        if (r == s) {
            return;
        }
    }
    moduleRoots.push_back(s);
}

const std::vector<std::string>& Type::getModuleRoots()
{
    return moduleRoots;
}

namespace
{
/// Is `file` inside `root` (a canonical directory), at a separator boundary?
bool underRoot(const std::string& file, const std::string& root)
{
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(file, ec);
    if (ec) {
        p = std::filesystem::path(file).lexically_normal();
    }
    const string f = p.generic_string();
    return f.size() > root.size() && f.compare(0, root.size(), root) == 0
        && f[root.size()] == '/';
}
}  // namespace

namespace
{
/// The locations a spec would import from: its origin (a file) and, for
/// a package, its search directories (a namespace package has only
/// those).  "built-in" and "frozen" are not paths and match no root.
std::vector<std::string> specLocations(PyObject* spec)
{
    std::vector<std::string> locations;
    PyObject* origin = PyObject_GetAttrString(spec, "origin");
    if (origin && PyUnicode_Check(origin)) {
        locations.emplace_back(PyUnicode_AsUTF8(origin));
    }
    Py_XDECREF(origin);
    PyObject* dirs = PyObject_GetAttrString(spec, "submodule_search_locations");
    if (dirs && PySequence_Check(dirs)) {
        PyObject* seq = PySequence_Fast(dirs, "locations");
        if (seq) {
            for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(seq); ++i) {
                PyObject* d = PySequence_Fast_GET_ITEM(seq, i);
                if (PyUnicode_Check(d)) {
                    locations.emplace_back(PyUnicode_AsUTF8(d));
                }
            }
            Py_DECREF(seq);
        }
    }
    Py_XDECREF(dirs);
    PyErr_Clear();
    return locations;
}

/// Where the code that produced a spec lives: the file of the module the
/// loader's class is defined in.  A spec with no location at all comes
/// from a meta-path finder -- FEM's femtools.migrate_app maps a saved
/// legacy name onto today's module -- and such a finder is admitted by
/// where it was defined, as the module it stands for would be.
std::string loaderHome(PyObject* spec)
{
    std::string home;
    PyObject* loader = PyObject_GetAttrString(spec, "loader");
    if (loader && loader != Py_None) {
        PyObject* modName = PyObject_GetAttrString((PyObject*)Py_TYPE(loader), "__module__");
        if (modName && PyUnicode_Check(modName)) {
            PyObject* mods = PyImport_GetModuleDict();
            PyObject* mod = mods ? PyDict_GetItem(mods, modName) : nullptr;
            if (mod) {
                PyObject* file = PyObject_GetAttrString(mod, "__file__");
                if (file && PyUnicode_Check(file)) {
                    home = PyUnicode_AsUTF8(file);
                }
                Py_XDECREF(file);
            }
        }
        Py_XDECREF(modName);
    }
    Py_XDECREF(loader);
    PyErr_Clear();
    return home;
}
}  // namespace

bool Type::moduleAllowed(const std::string& module)
{
    if (!Py_IsInitialized()) {
        return false;
    }
    PyGILStateLocker lock;
    // already loaded: nothing new runs
    PyObject* mods = PyImport_GetModuleDict();
    if (mods && PyDict_GetItemString(mods, module.c_str())) {
        return true;
    }
    PyObject* util = PyImport_ImportModule("importlib.util");
    if (!util) {
        PyErr_Clear();
        return false;
    }
    // Where would the import come from?  importlib.util.find_spec on a
    // top-level name imports nothing itself; on a dotted name it imports
    // the PARENT, so the name is walked one level at a time and a level
    // is looked up only once the levels above it are loaded or found
    // under a root -- a loaded stdlib package ("xml") does not admit an
    // unloaded submodule of its own ("xml.etree.ElementTree"), and the
    // parent an inner find_spec imports is one this check has admitted.
    bool allowed = true;
    std::string::size_type pos = 0;
    while (allowed && pos <= module.size()) {
        std::string::size_type dot = module.find('.', pos);
        if (dot == std::string::npos) {
            dot = module.size();
        }
        const std::string prefix = module.substr(0, dot);
        pos = dot + 1;
        if (mods && PyDict_GetItemString(mods, prefix.c_str())) {
            continue;
        }
        PyObject* spec = PyObject_CallMethod(util, "find_spec", "s", prefix.c_str());
        if (!spec) {
            // an invalid name, a broken finder: let the import report it
            PyErr_Clear();
            break;
        }
        if (spec == Py_None) {
            // no such module anywhere: the import fails with its own error
            Py_DECREF(spec);
            break;
        }
        std::vector<std::string> locations = specLocations(spec);
        if (locations.empty()) {
            // no file: a finder's answer -- judged by the finder's home
            // ("built-in" and "frozen" origins are locations, and match
            // no root)
            const std::string home = loaderHome(spec);
            if (!home.empty()) {
                locations.push_back(home);
            }
        }
        Py_DECREF(spec);
        bool under = false;
        for (const auto& loc : locations) {
            for (const auto& root : moduleRoots) {
                if (underRoot(loc, root)) {
                    under = true;
                    break;
                }
            }
            if (under) {
                break;
            }
        }
        allowed = under;
    }
    Py_DECREF(util);
    return allowed;
}

string Type::getModuleName(const char* ClassName)
{
    string temp(ClassName);
    std::string::size_type pos = temp.find_first_of("::");

    if (pos != std::string::npos) {
        return {temp, 0, pos};
    }
    return {};
}

Type Type::badType()
{
    Type bad;
    bad.index = 0;
    return bad;
}


Type Type::createType(const Type& parent, const char* name, instantiationMethod method)
{
    Type newType;
    newType.index = static_cast<unsigned int>(Type::typedata.size());
    TypeData* typeData = new TypeData(name, newType, parent, method);
    Type::typedata.push_back(typeData);

    // add to dictionary for fast lookup
    Type::typemap[name] = newType.getKey();

    return newType;
}


void Type::init()
{
    assert(Type::typedata.empty());


    Type::typedata.push_back(new TypeData("BadType"));
    Type::typemap["BadType"] = 0;
}

void Type::destruct()
{
    for (auto it : typedata) {
        delete it;
    }
    typedata.clear();
    typemap.clear();
    loadModuleSet.clear();
}

Type Type::fromName(const char* name)
{
    std::map<std::string, unsigned int>::const_iterator pos;

    pos = typemap.find(name);
    if (pos != typemap.end()) {
        return typedata[pos->second]->type;
    }

    return Type::badType();
}

Type Type::fromKey(unsigned int key)
{
    if (key < typedata.size()) {
        return typedata[key]->type;
    }

    return Type::badType();
}

const char* Type::getName() const
{
    return typedata[index]->name.c_str();
}

Type Type::getParent() const
{
    return typedata[index]->parent;
}

bool Type::isDerivedFrom(const Type& type) const
{

    Type temp(*this);
    do {
        if (temp == type) {
            return true;
        }
        temp = temp.getParent();
    } while (temp != badType());

    return false;
}

int Type::getAllDerivedFrom(const Type& type, std::vector<Type>& List)
{
    int cnt = 0;

    for (auto it : typedata) {
        if (it->type.isDerivedFrom(type)) {
            List.push_back(it->type);
            cnt++;
        }
    }
    return cnt;
}

int Type::getNumTypes()
{
    return static_cast<int>(typedata.size());
}

Type Type::getTypeIfDerivedFrom(const char* name, const Type& parent, bool bLoadModule)
{
    if (bLoadModule) {
        importModule(name);
    }

    Type type = fromName(name);

    if (type.isDerivedFrom(parent)) {
        return type;
    }

    return Type::badType();
}
