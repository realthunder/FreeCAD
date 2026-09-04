/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "ExpressionSecurity.h"

using json = nlohmann::json;

namespace App {
namespace ExpressionSecurity {

////////////////////////////////////////////////////////////////////////////////////
//
// Catalog v1 (frozen, docs/ExpressionSandboxPhase0.md sec 6.1)
//

const char *permissionName(Permission perm)
{
    switch (perm) {
    case Permission::DocReadSelf:   return "doc.read.self";
    case Permission::DocWriteSelf:  return "doc.write.self";
    case Permission::DocForeign:    return "doc.foreign";
    case Permission::GeomCall:      return "geom.call";
    case Permission::AppQuery:      return "app.query";
    case Permission::PrefsRead:     return "prefs.read";
    case Permission::Gui:           return "gui";
    case Permission::HostImport:    return "host.import";
    case Permission::UnsafeGetattr: return "unsafe.getattr";
    case Permission::PkgInstall:    return "pkg.install";
    }
    return "";
}

const char *decisionName(Decision decision)
{
    switch (decision) {
    case Decision::Allow:  return "allow";
    case Decision::Deny:   return "deny";
    case Decision::Prompt: return "prompt";
    }
    return "";
}

std::optional<Permission> permissionFromName(const std::string &name, std::string *target)
{
    static const std::pair<const char *, Permission> table[] = {
        {"doc.read.self",  Permission::DocReadSelf},
        {"doc.write.self", Permission::DocWriteSelf},
        {"doc.foreign",    Permission::DocForeign},
        {"geom.call",      Permission::GeomCall},
        {"app.query",      Permission::AppQuery},
        {"prefs.read",     Permission::PrefsRead},
        {"gui",            Permission::Gui},
        {"host.import",    Permission::HostImport},
        {"unsafe.getattr", Permission::UnsafeGetattr},
        {"pkg.install",    Permission::PkgInstall},
    };
    std::string base = name;
    std::string module;
    auto colon = name.find(':');
    if (colon != std::string::npos) {
        base = name.substr(0, colon);
        module = name.substr(colon + 1);
    }
    for (auto &entry : table) {
        if (base == entry.first) {
            // Only host.import and pkg.install are parameterized.
            if (!module.empty() && entry.second != Permission::HostImport
                    && entry.second != Permission::PkgInstall)
                return std::nullopt;
            if (target)
                *target = module;
            return entry.second;
        }
    }
    return std::nullopt;
}

std::optional<PrincipalClass> principalClass(const std::string &principal)
{
    if (principal == "session")
        return PrincipalClass::Session;
    if (principal.rfind("addon:", 0) == 0 && principal.size() > 6)
        return PrincipalClass::Addon;
    static const char docPrefix[] = "document:sha256:";
    if (principal.rfind(docPrefix, 0) == 0
            && principal.size() == sizeof(docPrefix) - 1 + 64)
        return PrincipalClass::Document;
    return std::nullopt;
}

Decision catalogDefault(PrincipalClass pclass, Permission perm)
{
    // The frozen v1 table. Addons default to ALLOW across the board (they
    // are trusted at install time); the rows below spell out document and
    // session.
    if (pclass == PrincipalClass::Addon)
        return Decision::Allow;
    const bool doc = (pclass == PrincipalClass::Document);
    switch (perm) {
    case Permission::DocReadSelf:   return Decision::Allow;
    case Permission::DocWriteSelf:  return Decision::Allow;
    case Permission::DocForeign:    return doc ? Decision::Prompt : Decision::Allow;
    case Permission::GeomCall:      return Decision::Allow;
    case Permission::AppQuery:      return doc ? Decision::Prompt : Decision::Allow;
    // a preference read through a curated reader is not a secret and
    // cannot write: allowed anywhere (2026-09-04)
    case Permission::PrefsRead:     return Decision::Allow;
    case Permission::Gui:           return doc ? Decision::Deny : Decision::Allow;
    case Permission::HostImport:    return Decision::Prompt;
    case Permission::UnsafeGetattr: return doc ? Decision::Deny : Decision::Prompt;
    // an install is always the user's click, whoever asked
    case Permission::PkgInstall:    return Decision::Prompt;
    }
    return Decision::Deny;
}

bool isPromptable(PrincipalClass pclass, Permission perm)
{
    // v1 marks exactly one cell not-promptable: gui for a document
    // principal. A document has no business driving the GUI, and no prompt
    // should offer to let it.
    return !(pclass == PrincipalClass::Document && perm == Permission::Gui);
}

std::optional<Permission> pseudoPropertyPermission(
        const std::string &pseudoName, std::string *hostModule)
{
    struct Entry {
        const char *name;
        Permission perm;
        const char *module;  // only for HostImport
    };
    static const Entry table[] = {
        {"_pla",     Permission::DocReadSelf,   nullptr},
        {"_matrix",  Permission::DocReadSelf,   nullptr},
        {"__pla",    Permission::DocReadSelf,   nullptr},
        {"__matrix", Permission::DocReadSelf,   nullptr},
        {"_ref",     Permission::DocReadSelf,   nullptr},
        {"_self",    Permission::UnsafeGetattr, nullptr},
        {"_shape",   Permission::GeomCall,      nullptr},
        {"_app",     Permission::AppQuery,      nullptr},
        {"_gui",     Permission::Gui,           nullptr},
        {"_part",    Permission::HostImport,    "Part"},
        {"_cq",      Permission::HostImport,    "freecad.fc_cadquery"},
        // _math/_re/_coll/_py: Ring-0 in-image modules, no permission --
        // isolation by image contents (they are simply absent from the
        // host side), so they fall through to nullopt below.
    };
    for (auto &entry : table) {
        if (pseudoName == entry.name) {
            if (hostModule)
                *hostModule = entry.module ? entry.module : "";
            return entry.perm;
        }
    }
    return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////////
//
// SHA-256 (FIPS 180-4). Local, dependency-free implementation so the
// service does not pull a crypto or Qt dependency into the App tier; the
// unit tests pin it to the standard test vectors.
//

namespace {

struct Sha256 {
    std::uint32_t state[8];
    std::uint64_t bitlen = 0;
    unsigned char buffer[64];
    std::size_t buflen = 0;

    Sha256()
    {
        static const std::uint32_t init[8] = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        };
        std::memcpy(state, init, sizeof(state));
    }

    static std::uint32_t rotr(std::uint32_t x, int n)
    {
        return (x >> n) | (x << (32 - n));
    }

    void transform(const unsigned char *chunk)
    {
        static const std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (std::uint32_t(chunk[i * 4]) << 24)
                 | (std::uint32_t(chunk[i * 4 + 1]) << 16)
                 | (std::uint32_t(chunk[i * 4 + 2]) << 8)
                 | std::uint32_t(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const unsigned char *data, std::size_t len)
    {
        bitlen += std::uint64_t(len) * 8;
        while (len) {
            std::size_t take = 64 - buflen;
            if (take > len)
                take = len;
            std::memcpy(buffer + buflen, data, take);
            buflen += take;
            data += take;
            len -= take;
            if (buflen == 64) {
                transform(buffer);
                buflen = 0;
            }
        }
    }

    void final(unsigned char digest[32])
    {
        // Padding: 0x80, zeros, then the 64-bit big-endian bit length.
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        std::uint64_t savedBitlen = bitlen - 8;  // update() above counted the pad
        while (buflen != 56)
            updateRaw(&zero, 1);
        unsigned char lenbuf[8];
        for (int i = 0; i < 8; ++i)
            lenbuf[i] = (unsigned char)(savedBitlen >> (56 - i * 8));
        updateRaw(lenbuf, 8);
        for (int i = 0; i < 8; ++i) {
            digest[i * 4] = (unsigned char)(state[i] >> 24);
            digest[i * 4 + 1] = (unsigned char)(state[i] >> 16);
            digest[i * 4 + 2] = (unsigned char)(state[i] >> 8);
            digest[i * 4 + 3] = (unsigned char)(state[i]);
        }
    }

private:
    // update() without bit accounting, for the padding tail.
    void updateRaw(const unsigned char *data, std::size_t len)
    {
        while (len) {
            std::size_t take = 64 - buflen;
            if (take > len)
                take = len;
            std::memcpy(buffer + buflen, data, take);
            buflen += take;
            data += take;
            len -= take;
            if (buflen == 64) {
                transform(buffer);
                buflen = 0;
            }
        }
    }
};

}  // namespace

std::string sha256Hex(const void *data, std::size_t len)
{
    Sha256 ctx;
    ctx.update(static_cast<const unsigned char *>(data), len);
    unsigned char digest[32];
    ctx.final(digest);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////////////
//
// Document-hash canonicalization v1 (frozen, sec 6.3)
//

void DocumentHashBuilder::addExpression(const std::string &expr)
{
    items.insert(std::string("e\x1F") + expr);
}

void DocumentHashBuilder::addCell(const std::string &expr)
{
    items.insert(std::string("c\x1F") + expr);
}

void DocumentHashBuilder::addScript(const std::string &moduleClass, const std::string &code)
{
    items.insert(std::string("s\x1F") + moduleClass + '\x1F' + code);
}

std::string DocumentHashBuilder::principalId() const
{
    // "fcexpr-v1\0" + the sorted, deduplicated canonical items joined by
    // '\0'. std::set already provides sorted-set semantics; a no-op resave
    // or a duplicate string cannot change the hash.
    std::string bytes("fcexpr-v1");
    bytes.push_back('\0');
    bool first = true;
    for (const auto &item : items) {
        if (!first)
            bytes.push_back('\0');
        first = false;
        bytes += item;
    }
    return std::string("document:sha256:") + sha256Hex(bytes.data(), bytes.size());
}

////////////////////////////////////////////////////////////////////////////////////
//
// Grant store (grants.json schema v1, frozen, sec 6.2)
//

bool GrantStore::load(const std::string &path, std::string *errMsg)
{
    _grants.clear();
    _defaults.clear();

    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        // A missing store is an empty store.
        return true;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception &e) {
        if (errMsg)
            *errMsg = e.what();
        return false;
    }

    try {
        int version = root.value("version", 0);
        if (version > 1) {
            // Migrate-forward only: a newer schema is not guessed at.
            if (errMsg)
                *errMsg = "grants.json schema version " + std::to_string(version)
                        + " is newer than this build understands";
            return false;
        }
        if (root.contains("defaults")) {
            for (auto &entry : root["defaults"].items()) {
                const std::string value = entry.value().get<std::string>();
                if (value == "allow")
                    _defaults[entry.key()] = Decision::Allow;
                else if (value == "deny")
                    _defaults[entry.key()] = Decision::Deny;
                else if (value == "prompt")
                    _defaults[entry.key()] = Decision::Prompt;
                // Unknown decision strings are ignored, not errors.
            }
        }
        if (root.contains("grants")) {
            for (auto &g : root["grants"]) {
                Grant grant;
                grant.principal = g.value("principal", "");
                grant.permission = g.value("permission", "");
                grant.target = g.value("target", "*");
                grant.allow = g.value("decision", "deny") == "allow";
                grant.grantedUtc = g.value("granted_utc", "");
                if (g.contains("display")) {
                    grant.displayLabel = g["display"].value("label", "");
                    grant.displayPath = g["display"].value("path", "");
                }
                // Normalize the parameterized form: a permission of
                // "host.import:<m>" folds the module into the target.
                std::string module;
                auto perm = permissionFromName(grant.permission, &module);
                if (!perm)
                    continue;  // unknown permission: skip, do not fail the store
                if (!module.empty())
                    grant.target = module;
                grant.permission = permissionName(*perm);
                if (grant.principal.empty() || !principalClass(grant.principal))
                    continue;  // unknown principal form: skip
                _grants.push_back(std::move(grant));
            }
        }
    } catch (const std::exception &e) {
        if (errMsg)
            *errMsg = e.what();
        _grants.clear();
        _defaults.clear();
        return false;
    }
    return true;
}

bool GrantStore::save(const std::string &path, std::string *errMsg) const
{
    json root;
    root["version"] = 1;
    json defaults = json::object();
    for (const auto &entry : _defaults)
        defaults[entry.first] = decisionName(entry.second);
    root["defaults"] = defaults;
    json grants = json::array();
    for (const auto &grant : _grants) {
        json g;
        g["principal"] = grant.principal;
        g["permission"] = grant.permission;
        g["target"] = grant.target;
        g["decision"] = grant.allow ? "allow" : "deny";
        g["scope"] = "always";
        g["granted_utc"] = grant.grantedUtc;
        json display;
        display["label"] = grant.displayLabel;
        display["path"] = grant.displayPath;
        g["display"] = display;
        grants.push_back(std::move(g));
    }
    root["grants"] = std::move(grants);

    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errMsg)
                *errMsg = "cannot write " + tmp;
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out) {
            if (errMsg)
                *errMsg = "write failed for " + tmp;
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (errMsg)
            *errMsg = "cannot rename " + tmp + " over " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

std::optional<Decision> GrantStore::lookupGrant(const std::string &principal,
        Permission perm, const std::string &target) const
{
    const char *permName = permissionName(perm);
    bool anyAllow = false;
    bool anyDeny = false;
    for (const auto &grant : _grants) {
        if (grant.principal != principal || grant.permission != permName)
            continue;
        if (grant.target != target && grant.target != "*")
            continue;
        if (grant.allow)
            anyAllow = true;
        else
            anyDeny = true;
    }
    // An explicit deny outranks any allow.
    if (anyDeny)
        return Decision::Deny;
    if (anyAllow)
        return Decision::Allow;
    return std::nullopt;
}

std::optional<Decision> GrantStore::lookup(const std::string &principal,
        Permission perm, const std::string &target) const
{
    auto granted = lookupGrant(principal, perm, target);
    if (granted)
        return granted;
    auto it = _defaults.find(permissionName(perm));
    if (it != _defaults.end())
        return it->second;
    return std::nullopt;
}

std::size_t GrantStore::remove(const std::string &principal,
        Permission perm, const std::string &target)
{
    const char *permName = permissionName(perm);
    std::size_t before = _grants.size();
    _grants.erase(std::remove_if(_grants.begin(), _grants.end(),
            [&](const Grant &g) {
                return g.principal == principal && g.permission == permName
                    && (target == "*" || g.target == target);
            }),
            _grants.end());
    return before - _grants.size();
}

void GrantStore::add(Grant grant)
{
    if (grant.grantedUtc.empty())
        grant.grantedUtc = utcNow();
    // Normalize as load() does.
    std::string module;
    auto perm = permissionFromName(grant.permission, &module);
    if (perm) {
        if (!module.empty())
            grant.target = module;
        grant.permission = permissionName(*perm);
    }
    if (grant.target.empty())
        grant.target = "*";
    _grants.push_back(std::move(grant));
}

std::size_t GrantStore::removePrincipal(const std::string &principal)
{
    std::size_t before = _grants.size();
    _grants.erase(std::remove_if(_grants.begin(), _grants.end(),
            [&principal](const Grant &g) { return g.principal == principal; }),
            _grants.end());
    return before - _grants.size();
}

void GrantStore::clear()
{
    _grants.clear();
    _defaults.clear();
}

////////////////////////////////////////////////////////////////////////////////////
//
// Audit log
//

AuditLog::AuditLog(std::string path_)
    : path(std::move(path_))
{}

bool AuditLog::append(const std::string &principal, Permission perm,
        const std::string &target, Decision decision, const std::string &context)
{
    json line;
    line["utc"] = utcNow();
    line["principal"] = principal;
    line["permission"] = permissionName(perm);
    line["target"] = target;
    line["decision"] = decisionName(decision);
    if (!context.empty())
        line["context"] = context;
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::app);
    if (!out)
        return false;
    out << line.dump() << '\n';
    return static_cast<bool>(out);
}

std::string utcNow()
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace ExpressionSecurity
}  // namespace App
