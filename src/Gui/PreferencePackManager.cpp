/***************************************************************************
 *   Copyright (c) 2021 Chris Hennes <chennes@pioneerlibrarysystem.org>    *
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
# include <memory>
# include <set>
# include <string_view>
# include <mutex>
#endif

#include <boost/filesystem.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include "PreferencePackManager.h"
#include "App/Metadata.h"
#include "Base/Parameter.h"
#include "Base/Interpreter.h"
#include "Base/Console.h"
#include "DockWindowManager.h"
#include "ThemeManager.h"
#include "ToolBarManager.h"

#include <App/Application.h>

#include <ctime> // For generating a timestamped filename


using namespace Gui;
using namespace xercesc;
namespace fs = boost::filesystem;

PreferencePack::PreferencePack(const fs::path& path, const App::Metadata& metadata) :
    _path(path), _metadata(metadata)
{
    if (!fs::exists(_path)) {
        throw std::runtime_error{ "Cannot access " + path.string() };
    }

    // Let a pack carry its own assets. Every prefix a theme can name has to be
    // covered here, or the pack cannot ship the file it asks for: a pack that
    // sets MenuStyleSheet or IconSet needs "qssm" and "iconset" just as much as
    // one setting StyleSheet needs "qss".
    //
    // rescan() rebuilds every PreferencePack, so appending unconditionally grew
    // the search paths without bound -- the same directory once per rescan.
    auto addSearchPath = [](const char* prefix, const std::string& dir) {
        const auto key = QString::fromUtf8(prefix);
        auto paths = QDir::searchPaths(key);
        const auto path = QString::fromStdString(dir);
        if (!paths.contains(path)) {
            paths.append(path);
            QDir::setSearchPaths(key, paths);
        }
    };

    addSearchPath("qss", _path.string());
    addSearchPath("css", _path.string());
    addSearchPath("overlay", (_path / "overlay").string());
    addSearchPath("qssm", (_path / "menu").string());
    addSearchPath("iconset", (_path / "iconsets").string());
}

std::string PreferencePack::name() const
{
    return _metadata.name();
}

bool PreferencePack::apply() const
{
    // Run the pre.FCMacro, if it exists: if it raises an exception, abort the process
    auto preMacroPath = _path / "pre.FCMacro";
    if (fs::exists(preMacroPath)) {
        try {
            Base::Interpreter().runFile(preMacroPath.string().c_str(), false);
        }
        catch (...) {
            Base::Console().Message("PreferencePack application aborted by the preferencePack's pre.FCMacro");
            return false;
        }
    }

    // Back up the old config file
    auto savedPreferencePacksDirectory = fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks";
    auto backupFile = savedPreferencePacksDirectory / "user.cfg.backup";
    try {
        fs::remove(backupFile);
    }
    catch (...) {}
    App::GetApplication().GetUserParameter().SaveDocument(backupFile.string().c_str());

    // Apply the config settings
    applyConfigChanges();

    // Run the Post.FCMacro, if it exists
    auto postMacroPath = _path / "post.FCMacro";
    if (fs::exists(postMacroPath)) {
        try {
            Base::Interpreter().runFile(postMacroPath.string().c_str(), false);
        }
        catch (...) {
            Base::Console().Message("PreferencePack application reverted by the preferencePack's post.FCMacro");
            App::GetApplication().GetUserParameter().LoadDocument(backupFile.string().c_str());
            return false;
        }
    }

    return true;
}


App::Metadata Gui::PreferencePack::metadata() const
{
    return _metadata;
}

fs::path Gui::PreferencePack::path() const
{
    return _path;
}

fs::path Gui::PreferencePack::configFile() const
{
    return _path / (_metadata.name() + ".cfg");
}

void PreferencePack::applyConfigChanges() const
{
    auto configFile = _path / (_metadata.name() + ".cfg");
    if (!fs::exists(configFile)) {
        return;
    }

    auto newParameters = ParameterManager::Create();
    newParameters->LoadDocument(configFile.string().c_str());

    // Inserting only writes the keys the pack contains, so on its own a theme is
    // a partial description of the look: whatever it omits survives from the
    // theme before it. A theme owns all of it, so clear that set first.
    const bool isTheme = _metadata.type() == "Theme";
    ThemeManager::Transition transition;
    if (isTheme) {
        transition = ThemeManager::beginThemeChange(*newParameters);
    }

    auto baseAppGroup = App::GetApplication().GetUserParameter().GetGroup("BaseApp");
    newParameters->GetGroup("BaseApp")->insertTo(baseAppGroup);

    if (isTheme) {
        ThemeManager::endThemeChange(transition);
        ThemeManager::setCurrentTheme(_metadata.name());
    }
    else {
        ThemeManager::forgetThemeIfAppearanceChanged(*newParameters);
    }
}

PreferencePackManager::PreferencePackManager()
{
    auto modPath = fs::path(App::Application::getUserAppDataDir()) / "Mod";
    auto savedPath = fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks";
    auto resourcePath = fs::path(App::Application::getResourceDir()) / "Gui" / "PreferencePacks";
    _preferencePackPaths.push_back(resourcePath);
    _preferencePackPaths.push_back(modPath);
    _preferencePackPaths.push_back(savedPath);
    rescan();

    // Housekeeping:
    DeleteOldBackups();
}

void PreferencePackManager::rescan()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _preferencePacks.clear();
    for (const auto& path : _preferencePackPaths) {
        if (fs::exists(path) && fs::is_directory(path)) {
            FindPreferencePacksInPackage(path);
            for (const auto& mod : fs::directory_iterator(path)) {
                if (fs::is_directory(mod)) {
                    FindPreferencePacksInPackage(mod);
                }
            }
        }
    }
}

void Gui::PreferencePackManager::AddPackToMetadata(const std::string &packName,
                                                  const std::string &type) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto savedPreferencePacksDirectory =
        fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks";
    fs::path preferencePackDirectory(savedPreferencePacksDirectory / packName);
    if (fs::exists(preferencePackDirectory) && !fs::is_directory(preferencePackDirectory))
        throw std::runtime_error("Cannot create " + savedPreferencePacksDirectory.string()
                                 + ": file with that name exists already");

    if (!fs::exists(preferencePackDirectory)) fs::create_directories(preferencePackDirectory);

    // Create or update the saved user preferencePacks package.xml metadata file
    std::unique_ptr<App::Metadata> metadata;
    if (fs::exists(savedPreferencePacksDirectory / "package.xml")) {
        metadata = std::make_unique<App::Metadata>(savedPreferencePacksDirectory / "package.xml");
    }
    else {
        metadata = std::make_unique<App::Metadata>();
        metadata->setName("User-Saved Preference Packs");
        std::stringstream str;
        str << "Generated automatically -- edits may be lost when saving new preference packs. To "
            << "distribute one or more of these packs:\n"
            << "    1) copy the entire SavedPreferencePacks directory to a convenient location,\n"
            << "    2) rename the directory (usually to the name of the preference pack you are "
            << "distributing),\n"
            << "    3) delete any subfolders containing packs you don't want to distribute,\n"
            << "    4) use git to initialize the directory as a git repository,\n"
            << "    5) push it to a remote git host,\n"
            << "    6) activate Developer Mode in the Addon Manager,\n"
            << "    7) use Developer Tools in the Addon Manager to update the metadata file,\n"
            << "    8) add, commit, and push the updated package.xml file,\n"
            << "    9) add your remote host to the custom repositories list in the Addon Manager"
            << " preferences,\n"
            << "   10) use the Addon Manager to install your preference pack locally for testing.";
        metadata->setDescription(str.str());
        metadata->addLicense(App::Meta::License("All Rights Reserved", fs::path()));
    }
    for (const auto &item : metadata->content()) {
        if (item.first == "preferencepack") {
            if (item.second.name() == packName) {
                // A pack with this name exists already. Its type may still be
                // wrong -- saving over a plain pack with a theme has to make it
                // one, or the theme list would never show it.
                if (item.second.type() == type)
                    return;
                metadata->removeContentItem("preferencepack", packName);
                break;
            }
        }
    }
    App::Metadata newPreferencePackMetadata;
    newPreferencePackMetadata.setName(packName);
    if (!type.empty())
        newPreferencePackMetadata.setType(type);

    metadata->addContentItem("preferencepack", newPreferencePackMetadata);
    metadata->write(savedPreferencePacksDirectory / "package.xml");
}

void Gui::PreferencePackManager::importConfig(const std::string& packName,
    const boost::filesystem::path& path, const std::string& type)
{
    AddPackToMetadata(packName, type);

    auto savedPreferencePacksDirectory =
        fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks";
    auto cfgFilename = savedPreferencePacksDirectory / packName / (packName + ".cfg");
    fs::copy_file(path, cfgFilename, fs::copy_options::overwrite_existing);
    rescan();
}

void Gui::PreferencePackManager::FindPreferencePacksInPackage(const fs::path &mod)
{
    try {
        TryFindPreferencePacksInPackage(mod);
    }
    catch (const std::exception& e) {
        Base::Console().Error("%s\n", e.what());
    }
    catch (...) {
        // Failed to read the metadata, or to create the preferencePack based on it...
        auto packageMetadataFile = mod / "package.xml";
        Base::Console().Error("Failed to read %s\n", packageMetadataFile.string().c_str());
    }
}

void PreferencePackManager::TryFindPreferencePacksInPackage(const boost::filesystem::path& mod)
{
    auto packageMetadataFile = mod / "package.xml";
    static const auto modDirectory = fs::path(App::Application::getUserAppDataDir()) / "Mod" / "SavedPreferencePacks";
    static const auto resourcePath = fs::path(App::Application::getResourceDir()) / "Gui" / "PreferencePacks";

    if (fs::exists(packageMetadataFile) && fs::is_regular_file(packageMetadataFile)) {
        App::Metadata metadata(packageMetadataFile);
        auto content = metadata.content();
        auto basename = mod.filename().string();
        if (mod == modDirectory)
            basename = "##USER_SAVED##";
        else if (mod == resourcePath)
            basename = "##BUILT_IN##";
        for (const auto& item : content) {
            if (item.first == "preferencepack") {
                if (isVisible(basename, item.second.name())) {
                    PreferencePack newPreferencePack(mod / item.second.name(), item.second);
                    _preferencePacks.insert(std::make_pair(newPreferencePack.name(), newPreferencePack));
                }
            }
        }
    }
}

std::vector<std::string> PreferencePackManager::preferencePackNames() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::string> names;
    for (const auto& preferencePack : _preferencePacks)
        names.push_back(preferencePack.first);
    return names;
}

std::map<std::string, PreferencePack> Gui::PreferencePackManager::preferencePacks() const
{
    return _preferencePacks;
}

bool PreferencePackManager::apply(const std::string& preferencePackName) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (auto preferencePack = _preferencePacks.find(preferencePackName); preferencePack != _preferencePacks.end()) {
        BackupCurrentConfig();
        bool wasApplied = preferencePack->second.apply();
        if (wasApplied) {
            // If the visibility state of the dock windows was changed we have to manually reload their state
            Gui::DockWindowManager* pDockMgr = Gui::DockWindowManager::instance();
            pDockMgr->loadState();

            // Same goes for toolbars:
            Gui::ToolBarManager* pToolbarMgr = Gui::ToolBarManager::getInstance();
            pToolbarMgr->restoreState();

            // TODO: Are there other things that have to be manually triggered?
        }
        return wasApplied;
    }
    else {
        throw std::runtime_error("No such Preference Pack: " + preferencePackName);
    }
}

fs::path PreferencePackManager::configFileFor(const std::string& preferencePackName) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto pack = _preferencePacks.find(preferencePackName);
    if (pack == _preferencePacks.end())
        return {};
    auto configFile = pack->second.configFile();
    return fs::exists(configFile) ? configFile : fs::path {};
}

std::string findUnusedName(const std::string &basename, ParameterGrp::handle parent)
{
    int i = 1;
    while (true) {
        std::ostringstream nameToTest;
        nameToTest << basename << "_" << i;
        if (!parent->HasGroup(nameToTest.str().c_str()))
            return nameToTest.str();
        ++i;
    }
}

bool PreferencePackManager::isVisible(const std::string& addonName, const std::string& preferencePackName) const
{
    if (addonName.empty() || preferencePackName.empty())
        return true;

    auto pref = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General/HiddenPreferencePacks");
    auto hiddenPacks = pref->GetGroups();
    auto hiddenPack = std::find_if(hiddenPacks.begin(), hiddenPacks.end(), [addonName, preferencePackName](ParameterGrp::handle handle) {
        return (handle->GetASCII("addonName", "") == addonName) && (handle->GetASCII("preferencePackName", "") == preferencePackName);
        });
    if (hiddenPack == hiddenPacks.end())
        return true;
    else
        return false;
}

void PreferencePackManager::toggleVisibility(const std::string& addonName, const std::string& preferencePackName)
{
    if (preferencePackName.empty())
        return;
    auto pref = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General/HiddenPreferencePacks");
    auto hiddenPacks = pref->GetGroups();
    auto hiddenPack = std::find_if(hiddenPacks.begin(), hiddenPacks.end(), [addonName,preferencePackName](ParameterGrp::handle handle) {
        return (handle->GetASCII("addonName", "") == addonName) && (handle->GetASCII("preferencePackName", "") == preferencePackName);
        });
    if (hiddenPack == hiddenPacks.end()) {
        auto name = findUnusedName("PreferencePack", pref);
        auto group = pref->GetGroup(name.c_str());
        group->SetASCII("addonName", addonName.c_str());
        group->SetASCII("preferencePackName", preferencePackName.c_str());
    }
    else {
        auto groupName = (*hiddenPack)->GetGroupName();
        hiddenPacks.clear(); // To decrement the reference count of the group we are about the remove...
        pref->RemoveGrp(groupName);
    }
    rescan();
}

void Gui::PreferencePackManager::deleteUserPack(const std::string& name)
{
    if (name.empty())
        return;
    auto savedPreferencePacksDirectory = fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks";
    auto savedPath = savedPreferencePacksDirectory / name;
    std::unique_ptr<App::Metadata> metadata;
    if (fs::exists(savedPreferencePacksDirectory / "package.xml")) {
        metadata = std::make_unique<App::Metadata>(savedPreferencePacksDirectory / "package.xml");
    }
    else {
        throw std::runtime_error("Lost the user-saved preference packs metadata file!");
    }
    metadata->removeContentItem("preferencepack", name);
    metadata->write(savedPreferencePacksDirectory / "package.xml");
    if (fs::exists(savedPath))
        fs::remove_all(savedPath);
    rescan();
}

void copyTemplateParameters(Base::Reference<ParameterGrp> templateGroup, const std::string& path, Base::Reference<ParameterGrp> outputGroup)
{
    auto userParameterHandle = App::GetApplication().GetParameterGroupByPath(path.c_str());

    // Ensure that the DockWindowManager has saved its current state:
    Gui::DockWindowManager* pDockMgr = Gui::DockWindowManager::instance();
    pDockMgr->saveState();

    // Do the same for ToolBars
    Gui::ToolBarManager* pToolbarMgr = Gui::ToolBarManager::getInstance();
    pToolbarMgr->saveState();

    auto boolMap = templateGroup->GetBoolMap();
    for (const auto& kv : boolMap) {
        auto currentValue = userParameterHandle->GetBool(kv.first.c_str(), kv.second);
        outputGroup->SetBool(kv.first.c_str(), currentValue);
    }

    auto intMap = templateGroup->GetIntMap();
    for (const auto& kv : intMap) {
        auto currentValue = userParameterHandle->GetInt(kv.first.c_str(), kv.second);
        outputGroup->SetInt(kv.first.c_str(), currentValue);
    }

    auto uintMap = templateGroup->GetUnsignedMap();
    for (const auto& kv : uintMap) {
        auto currentValue = userParameterHandle->GetUnsigned(kv.first.c_str(), kv.second);
        outputGroup->SetUnsigned(kv.first.c_str(), currentValue);
    }

    auto floatMap = templateGroup->GetFloatMap();
    for (const auto& kv : floatMap) {
        auto currentValue = userParameterHandle->GetFloat(kv.first.c_str(), kv.second);
        outputGroup->SetFloat(kv.first.c_str(), currentValue);
    }

    auto asciiMap = templateGroup->GetASCIIMap();
    for (const auto& kv : asciiMap) {
        auto currentValue = userParameterHandle->GetASCII(kv.first.c_str(), kv.second.c_str());
        outputGroup->SetASCII(kv.first.c_str(), currentValue.c_str());
    }

    // Recurse...
    auto templateSubgroups = templateGroup->GetGroups();
    for (auto& templateSubgroup : templateSubgroups) {
        std::string sgName = templateSubgroup->GetGroupName();
        auto outputSubgroupHandle = outputGroup->GetGroup(sgName.c_str());
        copyTemplateParameters(templateSubgroup, path + "/" + sgName, outputSubgroupHandle);
    }
}

void copyTemplateParameters(/*const*/ ParameterManager& templateParameterManager, ParameterManager& outputParameterManager)
{
    auto groups = templateParameterManager.GetGroups();
    for (auto& group : groups) {
        std::string name = group->GetGroupName();
        auto groupHandle = outputParameterManager.GetGroup(name.c_str());
        copyTemplateParameters(group, "User parameter:" + name, groupHandle);
    }
}

namespace
{

/**
 * An appearance parameter that names a file, and the Qt search-path prefix its
 * value is resolved through. The pack directory is registered on every one of
 * these prefixes (see PreferencePack's constructor), which is what lets a pack
 * carry the files it asks for.
 */
struct AssetKey
{
    const char* name;
    const char* prefix;
    bool isList;  ///< a ';'-separated list, read left to right
};

const std::vector<AssetKey>& assetKeys()
{
    static const std::vector<AssetKey> keys {
        {"StyleSheet", "qss", false},
        {"OverlayActiveStyleSheet", "overlay", false},
        {"MenuStyleSheet", "qssm", false},
        {"IconSet", "iconset", true},
    };
    return keys;
}

/// Where a pack keeps files named through a given prefix, relative to its root.
QString packSubdir(const QString& prefix)
{
    if (prefix == QLatin1String("overlay")) {
        return QStringLiteral("overlay");
    }
    if (prefix == QLatin1String("qssm")) {
        return QStringLiteral("menu");
    }
    if (prefix == QLatin1String("iconset")) {
        return QStringLiteral("iconsets");
    }
    return {};  // "qss" and "css" are read from the pack root
}

/// Resolve a name the way the code that consumes it does: a path that exists as
/// given, otherwise through the prefix's search paths.
QFileInfo resolveAsset(const QString& prefix, const QString& name)
{
    if (name.isEmpty()) {
        return {};
    }
    if (QFile::exists(name)) {
        return QFileInfo(name);
    }
    const QString prefixed = prefix + QLatin1Char(':') + name;
    if (QFile::exists(prefixed)) {
        return QFileInfo(prefixed);
    }
    return {};
}

/// A file FreeCAD ships resolves on every machine, so a pack gains nothing by
/// carrying a copy of it.
bool isShipped(const QFileInfo& file)
{
    if (file.filePath().startsWith(QLatin1Char(':'))) {
        return true;  // compiled into the binary
    }
    static const QString resourceDir =
        QDir(QString::fromUtf8(App::Application::getResourceDir().c_str())).absolutePath()
        + QLatin1Char('/');
    return file.absoluteFilePath().startsWith(resourceDir);
}

void copyAssetIntoPack(const fs::path& packDir,
                       const QString& prefix,
                       const QString& name,
                       const QFileInfo& file,
                       std::set<QString>& seen);

/**
 * Follow what a stylesheet or icon set refers to. Both name other files the
 * same way the parameters do -- "qss:images/close.svg", "iconset:My/tree.svg"
 * -- and an icon set can also "#import" another one. A pack that carried only
 * the file it names would still be missing every image in it.
 */
void copyReferencedAssets(const fs::path& packDir, const QFileInfo& file, std::set<QString>& seen)
{
    const auto suffix = file.suffix().toLower();
    if (suffix != QLatin1String("qss") && suffix != QLatin1String("css")
        && suffix != QLatin1String("txt")) {
        return;
    }

    QFile input(file.filePath());
    if (!input.open(QFile::ReadOnly | QFile::Text)) {
        return;
    }
    QTextStream stream(&input);
    const QString content = stream.readAll();
    input.close();

    static const QRegularExpression reference(
        QStringLiteral(R"((qss|css|overlay|qssm|iconset):([^\s"')<>;,]+))"));
    auto references = reference.globalMatch(content);
    while (references.hasNext()) {
        const auto match = references.next();
        const QString prefix = match.captured(1);
        const QString name = match.captured(2);
        const QFileInfo target = resolveAsset(prefix, name);
        if (target.exists() && !isShipped(target)) {
            copyAssetIntoPack(packDir, prefix, name, target, seen);
        }
    }

    static const QRegularExpression import(QStringLiteral(R"(^[ \t]*#import[ \t]+(.+?)[ \t]*$)"),
                                           QRegularExpression::MultilineOption);
    auto imports = import.globalMatch(content);
    while (imports.hasNext()) {
        const QString name = imports.next().captured(1);
        const QFileInfo target = resolveAsset(QStringLiteral("iconset"), name);
        if (target.exists() && !isShipped(target)) {
            copyAssetIntoPack(packDir, QStringLiteral("iconset"), name, target, seen);
        }
    }
}

/**
 * Copy one file into the pack and recurse into what it refers to. \a name is
 * the name the file is known by, and it keeps that name inside the pack: a
 * reference to "images/close.svg" resolves only if the pack stores it under
 * that same relative path.
 */
void copyAssetIntoPack(const fs::path& packDir,
                       const QString& prefix,
                       const QString& name,
                       const QFileInfo& file,
                       std::set<QString>& seen)
{
    const QString canonical = file.canonicalFilePath();
    if (canonical.isEmpty() || !seen.insert(canonical).second) {
        return;  // already carried, or an import cycle
    }

    fs::path destination = packDir;
    const auto subdir = packSubdir(prefix);
    if (!subdir.isEmpty()) {
        destination /= subdir.toStdString();
    }
    destination /= fs::path(name.toStdString());

    const fs::path source(canonical.toStdString());
    try {
        fs::create_directories(destination.parent_path());
        // Re-saving a pack over itself resolves its own copy: nothing to do.
        if (!fs::exists(destination) || !fs::equivalent(source, destination)) {
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
        }
    }
    catch (const std::exception& e) {
        Base::Console().Warning("Preference pack could not carry %s: %s\n",
                                canonical.toUtf8().constData(), e.what());
        return;
    }

    copyReferencedAssets(packDir, file, seen);
}

/**
 * Make a saved pack self-contained. Every appearance asset it names that
 * FreeCAD does not ship is copied in, and the parameter is rewritten to the
 * name the pack stores it under. Without this a pack naming a stylesheet from
 * the author's own Gui/Stylesheets directory is broken on every other machine.
 */
void bundlePackAssets(const fs::path& packDir, ParameterManager& parameters)
{
    // Walk rather than GetGroup() the whole way: the latter creates what it
    // walks through, which would put empty groups into the saved pack.
    Base::Reference<ParameterGrp> group(&parameters);
    for (const char* name : {"BaseApp", "Preferences", "MainWindow"}) {
        if (!group->HasGroup(name)) {
            return;
        }
        group = group->GetGroup(name);
    }

    std::set<QString> seen;
    for (const auto& key : assetKeys()) {
        const QString value = QString::fromStdString(group->GetASCII(key.name));
        if (value.isEmpty()) {
            continue;
        }

        const QString prefix = QString::fromUtf8(key.prefix);
        const QStringList names = key.isList
            ? value.split(QLatin1Char(';'), Qt::SkipEmptyParts)
            : QStringList {value};

        QStringList stored;
        for (const auto& entry : names) {
            const QString name = entry.trimmed();
            const QFileInfo file = resolveAsset(prefix, name);
            if (!file.exists()) {
                Base::Console().Warning("Preference pack names %s, which cannot be found, "
                                        "so the pack cannot carry it\n",
                                        name.toUtf8().constData());
                stored << name;
                continue;
            }
            if (isShipped(file)) {
                stored << name;  // resolves anywhere FreeCAD is installed
                continue;
            }

            // An absolute path means nothing on another machine, and nothing
            // inside the pack either; the file keeps only its own name.
            const QString packName = QFileInfo(name).isAbsolute() ? file.fileName() : name;
            copyAssetIntoPack(packDir, prefix, packName, file, seen);
            stored << packName;
        }

        group->SetASCII(key.name, stored.join(QLatin1Char(';')).toUtf8().constData());
    }
}

/// Copy a parameter group whole -- every key, with its type, and every subgroup.
void copyGroupVerbatim(const Base::Reference<ParameterGrp>& from,
                       const Base::Reference<ParameterGrp>& to)
{
    for (const auto& entry : from->GetBoolMap()) {
        to->SetBool(entry.first.c_str(), entry.second);
    }
    for (const auto& entry : from->GetIntMap()) {
        to->SetInt(entry.first.c_str(), entry.second);
    }
    for (const auto& entry : from->GetUnsignedMap()) {
        to->SetUnsigned(entry.first.c_str(), entry.second);
    }
    for (const auto& entry : from->GetFloatMap()) {
        to->SetFloat(entry.first.c_str(), entry.second);
    }
    for (const auto& entry : from->GetASCIIMap()) {
        to->SetASCII(entry.first.c_str(), entry.second.c_str());
    }
    for (const auto& subgroup : from->GetGroups()) {
        const std::string name = subgroup->GetGroupName();
        copyGroupVerbatim(subgroup, to->GetGroup(name.c_str()));
    }
}

}  // namespace

void PreferencePackManager::save(const std::string& name,
                                 const std::vector<TemplateFile>& templates,
                                 const std::string& type)
{
    if (templates.empty())
        return;

    AddPackToMetadata(name, type);

    // Create the config file
    auto outputParameterManager = ParameterManager::Create();
    outputParameterManager->CreateDocument();
    for (const auto& t : templates) {
        auto templateParameterManager = ParameterManager::Create();
        templateParameterManager->LoadDocument(t.path.string().c_str());
        copyTemplateParameters(*templateParameterManager, *outputParameterManager);
    }
    // A theme's stylesheet variables are its own, and a template can only name
    // keys it knows about, so copyTemplateParameters cannot reach them. Take
    // the group whole.
    if (type == "Theme") {
        auto hThemes = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Themes");
        if (hThemes->HasGroup("Variables")) {
            copyGroupVerbatim(hThemes->GetGroup("Variables"),
                              outputParameterManager->GetGroup("BaseApp")
                                  ->GetGroup("Preferences")
                                  ->GetGroup("Themes")
                                  ->GetGroup("Variables"));
        }
    }

    auto savedPreferencePacksDirectory =
        fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks";
    auto packDirectory = savedPreferencePacksDirectory / name;

    // A pack that only points at the author's own stylesheet is not something
    // anyone else can install, so hand it its assets before writing the config.
    bundlePackAssets(packDirectory, *outputParameterManager);

    auto cfgFilename = packDirectory / (name + ".cfg");
    outputParameterManager->SaveDocument(cfgFilename.string().c_str());
}

// Needed until we support only C++20 and above and can use std::string's built-in ends_with()
bool fc_ends_with(std::string_view str, std::string_view suffix)
{
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<fs::path> scanForTemplateFolders(const std::string& groupName, const fs::path& entry)
{
    // From this location, find the folder(s) called "PreferencePackTemplates"
    std::vector<fs::path> templateFolders;
    if (fs::exists(entry)) {
        if (fs::is_directory(entry)) {
            if (entry.filename() == "PreferencePackTemplates" ||
                entry.filename() == "preference_pack_templates") {
                templateFolders.push_back(entry);
            }
            else {
                std::string subgroupName = groupName + "/" + entry.filename().string();
                for (const auto& subentry : fs::directory_iterator(entry)) {
                    auto contents = scanForTemplateFolders(subgroupName, subentry);
                    std::copy(contents.begin(), contents.end(), std::back_inserter(templateFolders));
                }
            }
        }
    }
    return templateFolders;
}

std::vector<PreferencePackManager::TemplateFile> scanForTemplateFiles(const std::string& groupName, const fs::path& entry)
{
    auto templateFolders = scanForTemplateFolders(groupName, entry);

    std::vector<PreferencePackManager::TemplateFile> templateFiles;
    for (const auto& templateDir : templateFolders) {
        if (!fs::exists(templateDir) || !fs::is_directory(templateDir))
            continue;
        for (const auto& entry : fs::directory_iterator(templateDir)) {
            if (entry.path().extension() == ".cfg") {
                auto name = entry.path().filename().stem().string();
                std::replace(name.begin(), name.end(), '_', ' ');
                // Make sure we don't insert the same thing twice...
                if (std::find_if(templateFiles.begin(), templateFiles.end(), [groupName, name](const auto &rhs)->bool {
                    return groupName == rhs.group && name == rhs.name;
                    } ) != templateFiles.end())
                    continue;
                templateFiles.push_back({ groupName, name, entry });
            }
        }
    }
    return templateFiles;
}

std::vector<PreferencePackManager::TemplateFile> PreferencePackManager::templateFiles(bool rescan)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_templateFiles.empty() && !rescan)
        return _templateFiles;

    // Locate all of the template files available on this system
    // Template files end in ".cfg" -- They are located in:
    // * $INSTALL_DIR/data/Gui/PreferencePackTemplates/(Appearance|Behavior)/*
    // * $DATA_DIR/Mod/**/PreferencePackTemplates/(Appearance|Behavior)/*
    // (alternate spellings are provided for packages using CamelCase and snake_case, and both major English dialects)

    auto resourcePath = fs::path(App::Application::getResourceDir()) / "Gui";
    auto modPath = fs::path(App::Application::getUserAppDataDir()) / "Mod";

    std::string group = "Built-In";
    if (fs::exists(resourcePath) && fs::is_directory(resourcePath)) {
        const auto localFiles = scanForTemplateFiles(group, resourcePath);
        std::copy(localFiles.begin(), localFiles.end(), std::back_inserter(_templateFiles));
    }

    if (fs::exists(modPath) && fs::is_directory(modPath)) {
        for (const auto& mod : fs::directory_iterator(modPath)) {
            group = mod.path().filename().string();
            const auto localFiles = scanForTemplateFiles(group, mod);
            std::copy(localFiles.begin(), localFiles.end(), std::back_inserter(_templateFiles));
        }
    }

    return _templateFiles;
}

void Gui::PreferencePackManager::BackupCurrentConfig() const
{
    auto backupDirectory = fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks" / "Backups";
    fs::create_directories(backupDirectory);

    // Create a timestamped filename:
    auto time = std::time(nullptr);
    std::ostringstream timestampStream;
    timestampStream << "user." << time << ".cfg";
    auto filename = backupDirectory / timestampStream.str();

    // Save the current config:
    App::GetApplication().GetUserParameter().SaveDocument(filename.string().c_str());
}

void Gui::PreferencePackManager::DeleteOldBackups() const
{
    constexpr auto oneWeek = 60.0 * 60.0 * 24.0 * 7.0;
    const auto now = std::time(nullptr);
    auto backupDirectory = fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks" / "Backups";
    if (fs::exists(backupDirectory) && fs::is_directory(backupDirectory)) {
        for (const auto& backup : fs::directory_iterator(backupDirectory)) {
            if (std::difftime(now, fs::last_write_time(backup)) > oneWeek) {
                try {
                    fs::remove(backup);
                }
                catch (...) {}
            }
        }
    }
}

std::vector<boost::filesystem::path> Gui::PreferencePackManager::configBackups() const
{
    std::vector<boost::filesystem::path> results;
    auto backupDirectory = fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks" / "Backups";
    if (fs::exists(backupDirectory) && fs::is_directory(backupDirectory)) {
        for (const auto& backup : fs::directory_iterator(backupDirectory)) {
            results.push_back(backup);
        }
    }
    return results;
}
