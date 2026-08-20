// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <map>
#include <mutex>

#include <Base/Console.h>

#include "MaterialCards.h"
#include "MaterialLoader.h"
#include "MaterialManager.h"
#include "Materials.h"

using namespace Materials;

namespace
{

// Content plus the provenance stamped on it, see MaterialCards::load().
std::string cacheKey(const std::string& hash, const QString& uuid, const QString& name)
{
    return hash + '\n' + uuid.toStdString() + '\n' + name.toStdString();
}

std::mutex& cacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::weak_ptr<const Material>>& cache()
{
    static std::map<std::string, std::weak_ptr<const Material>> entries;
    return entries;
}

std::shared_ptr<const Material> lookup(const std::string& key)
{
    auto& entries = cache();
    auto it = entries.find(key);
    if (it == entries.end()) {
        return {};
    }
    if (auto card = it->second.lock()) {
        return card;
    }
    // The last referrer is gone: drop the entry rather than leaving the map to
    // grow one dead weak_ptr per card the session has ever seen.
    entries.erase(it);
    return {};
}

}  // namespace

std::shared_ptr<const Material> MaterialCards::find(const std::string& hash,
                                                    const QString& uuid,
                                                    const QString& name)
{
    std::lock_guard<std::mutex> guard(cacheMutex());
    return lookup(cacheKey(hash, uuid, name));
}

void MaterialCards::insert(const std::string& hash,
                           const QString& uuid,
                           const QString& name,
                           const std::shared_ptr<const Material>& card)
{
    if (!card) {
        return;
    }
    std::lock_guard<std::mutex> guard(cacheMutex());
    cache()[cacheKey(hash, uuid, name)] = card;
}

namespace
{

std::mutex& presetMutex()
{
    static std::mutex mutex;
    return mutex;
}

// Strong references: these cards are the library's own, and the library holds
// them for the life of the process anyway.
std::map<std::string, std::shared_ptr<const Material>>& presets()
{
    static std::map<std::string, std::shared_ptr<const Material>> index;
    return index;
}

bool& presetsBuilt()
{
    static bool built = false;
    return built;
}

}  // namespace

std::shared_ptr<const Material> MaterialCards::adopt(const std::string& hash,
                                                     const QString& uuid,
                                                     const QString& name,
                                                     const std::shared_ptr<const Material>& card)
{
    if (!card) {
        return {};
    }
    if (card->getUUID() == uuid && card->getName() == name) {
        // The referrer calls it what the card calls itself, so there is
        // nothing to stamp and every referrer can share this one instance.
        insert(hash, uuid, name, card);
        return card;
    }

    const std::string key = cacheKey(hash, uuid, name);
    {
        std::lock_guard<std::mutex> guard(cacheMutex());
        if (auto existing = lookup(key)) {
            return existing;
        }
    }
    auto stamped = std::make_shared<Material>(*card);
    stamped->setUUID(uuid);
    stamped->setName(name);
    std::shared_ptr<const Material> result = std::move(stamped);

    std::lock_guard<std::mutex> guard(cacheMutex());
    if (auto existing = lookup(key)) {
        return existing;
    }
    cache()[key] = result;
    return result;
}

std::shared_ptr<const Material> MaterialCards::preset(const std::string& hash)
{
    std::lock_guard<std::mutex> guard(presetMutex());
    auto& index = presets();
    if (!presetsBuilt()) {
        presetsBuilt() = true;
        auto materials = MaterialManager::getManager().getLocalMaterials();
        if (materials) {
            for (const auto& entry : *materials) {
                if (entry.second) {
                    index[entry.second->getContentHash()] = entry.second;
                }
            }
        }
        Base::Console().log("MaterialCards: indexed %d installed cards by content\n",
                            static_cast<int>(index.size()));
    }

    auto found = index.find(hash);
    return found == index.end() ? std::shared_ptr<const Material>() : found->second;
}

void MaterialCards::clearPresets()
{
    std::lock_guard<std::mutex> guard(presetMutex());
    presets().clear();
    presetsBuilt() = false;
}

std::shared_ptr<const Material> MaterialCards::load(const std::string& hash,
                                                    const QString& path,
                                                    const QString& uuid,
                                                    const QString& name)
{
    const std::string key = cacheKey(hash, uuid, name);
    {
        std::lock_guard<std::mutex> guard(cacheMutex());
        if (auto card = lookup(key)) {
            return card;
        }
    }

    // Parsed outside the lock: it reads a file, and holding a mutex across
    // that would serialize every document opening at once for no gain. Two
    // threads racing on the same content parse it twice and one of the two
    // instances is dropped, which costs a parse and breaks nothing.
    auto parsed = MaterialLoader::getMaterialFromFile(path);
    if (!parsed) {
        return {};
    }
    // Identity is the property's, not the file's: canonical content carries
    // the nil uuid and takes its name from whatever the blob file is called.
    parsed->setUUID(uuid);
    parsed->setName(name);
    std::shared_ptr<const Material> card = std::move(parsed);

    std::lock_guard<std::mutex> guard(cacheMutex());
    if (auto existing = lookup(key)) {
        return existing;
    }
    cache()[key] = card;
    return card;
}

std::size_t MaterialCards::size()
{
    std::lock_guard<std::mutex> guard(cacheMutex());
    auto& entries = cache();
    for (auto it = entries.begin(); it != entries.end();) {
        it = it->second.expired() ? entries.erase(it) : std::next(it);
    }
    return entries.size();
}
