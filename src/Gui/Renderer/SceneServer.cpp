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
 ****************************************************************************/

#include "SceneServer.h"
#include "SceneDump.h"
#include "SceneLadder.h"
#include "MeshSource.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Render;

namespace {

/// Compact SHA-1 (RFC 3174) for the WebSocket handshake accept key.
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE,
                     0x10325476, 0xC3D2E1F0};
    uint8_t block[64];
    uint64_t total = 0;
    size_t fill = 0;

    static uint32_t rol(uint32_t v, int n)
    { return (v << n) | (v >> (32 - n)); }

    void process(const uint8_t *p)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[4*i]) << 24) | (uint32_t(p[4*i+1]) << 16)
                 | (uint32_t(p[4*i+2]) << 8) | uint32_t(p[4*i+3]);
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void update(const void *data, size_t size)
    {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        total += size;
        while (size) {
            size_t n = std::min(size, sizeof(block) - fill);
            std::memcpy(block + fill, p, n);
            fill += n; p += n; size -= n;
            if (fill == sizeof(block)) {
                process(block);
                fill = 0;
            }
        }
    }

    void finish(uint8_t digest[20])
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (fill != 56)
            update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i)
            len[i] = uint8_t(bits >> (56 - 8 * i));
        update(len, 8);
        for (int i = 0; i < 5; ++i) {
            digest[4*i]   = uint8_t(h[i] >> 24);
            digest[4*i+1] = uint8_t(h[i] >> 16);
            digest[4*i+2] = uint8_t(h[i] >> 8);
            digest[4*i+3] = uint8_t(h[i]);
        }
    }
};

std::string base64(const uint8_t *data, size_t size)
{
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < size) v |= uint32_t(data[i+1]) << 8;
        if (i + 2 < size) v |= uint32_t(data[i+2]);
        out.push_back(tab[(v >> 18) & 63]);
        out.push_back(tab[(v >> 12) & 63]);
        out.push_back(i + 1 < size ? tab[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? tab[v & 63] : '=');
    }
    return out;
}

} // namespace

class SceneStreamServer::Private {
public:
    std::mutex mutex;
    int listenFd = -1;
    bool started = false;

    /// The shared door secret (docs/MultiDocServe.md §4/§8). Its own
    /// mutex, taken with no other lock held on either side, so it can
    /// never participate in an ordering cycle.
    std::mutex tokenMutex;
    std::string tokenSecret;

    std::string tokenNow()
    {
        std::lock_guard<std::mutex> guard(tokenMutex);
        return tokenSecret;
    }

    /// Whether a forwarded client address is believed (SceneServer.h,
    /// setTrustProxy). Atomic rather than mutexed: one bool read on
    /// every accepted connection.
    std::atomic<bool> trustProxy{false};

    /// The configured identity header name (SceneServer.h,
    /// setIdentityHeader), stored lowercase; empty = recognize the
    /// well-known ones. Guarded by tokenMutex, like the token — read
    /// once per accepted connection.
    std::string identityHeaderName;

    /// The client address to believe for a connection whose socket
    /// peer is \a peerIp, given the request head \a req. Empty unless
    /// trust is on, the peer is loopback, and a header named one.
    ///
    /// The peer check is the whole security of this: a proxy or the
    /// local end of a tunnel is on this machine, so anything arriving
    /// from elsewhere is talking to us directly and its headers are
    /// its own invention.
    std::string forwardedFor(const std::string &req,
                             const std::string &peerIp)
    {
        if (!trustProxy.load())
            return {};
        if (peerIp != "127.0.0.1" && peerIp != "::1")
            return {};
        // Cloudflare guarantees CF-Connecting-IP (docs/ShareAccess.md
        // §5); X-Forwarded-For is everyone else's spelling, a list of
        // client, proxy, proxy… whose first entry is the one that
        // reached the outermost proxy.
        std::string value = headerValue(req, "cf-connecting-ip");
        if (value.empty())
            value = headerValue(req, "x-forwarded-for");
        if (value.empty())
            return {};
        auto comma = value.find(',');
        if (comma != std::string::npos)
            value.resize(comma);
        auto b = value.find_first_not_of(" \t");
        auto e = value.find_last_not_of(" \t");
        if (b == std::string::npos)
            return {};
        value = value.substr(b, e - b + 1);
        // It lands in a roster the host reads and in nothing else, but
        // it is still attacker-shaped text: keep it to what an address
        // can be made of.
        if (value.size() > 45)
            return {};
        for (char c : value) {
            if (!std::isxdigit(static_cast<unsigned char>(c))
                    && c != '.' && c != ':')
                return {};
        }
        return value;
    }

    /// The verified identity an authenticating front door asserted on
    /// this request (docs/ShareAccess.md §4), under the same trust
    /// rule as forwardedFor: trust on, loopback peer, or the header
    /// is the client's own invention. Empty when nothing asserted one.
    std::string assertedIdentity(const std::string &req,
                                 const std::string &peerIp)
    {
        if (!trustProxy.load())
            return {};
        if (peerIp != "127.0.0.1" && peerIp != "::1")
            return {};
        std::string configured;
        {
            std::lock_guard<std::mutex> guard(tokenMutex);
            configured = identityHeaderName;
        }
        std::string value;
        if (!configured.empty()) {
            value = headerValue(req, configured.c_str());
        } else {
            // The front doors we know of (SceneServer.h,
            // setIdentityHeader): Cloudflare Access, oauth2-proxy,
            // and the generic spelling ngrok and others use.
            static const char *const wellKnown[] = {
                "cf-access-authenticated-user-email",
                "x-auth-request-email",
                "x-forwarded-email",
            };
            for (const char *name : wellKnown) {
                value = headerValue(req, name);
                if (!value.empty())
                    break;
            }
        }
        // Roster-bound attacker-shaped text, like the forwarded
        // address: bounded, printable, no control characters.
        if (value.size() > 128)
            return {};
        for (char c : value) {
            unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u == 0x7f)
                return {};
        }
        return value;
    }

    /// Whether a request carrying \a query may pass the door: open
    /// server, or a matching `?token=`.
    bool tokenOk(const std::string &query)
    {
        std::string secret = tokenNow();
        return secret.empty() || queryValue(query, "token") == secret;
    }

    /// The live grant list (SceneServer.h, setGrants) — the door when
    /// non-empty. Guarded by tokenMutex like the rest of the door
    /// configuration.
    std::vector<SceneGrant> grantList;
    uint64_t grantIdCounter = 0;

    /// Case-insensitive shell-wildcard match (`*`, `?`) — the same
    /// semantics as the sharing UI's rule patterns, without Qt.
    static bool globMatch(const std::string &pattern,
                          const std::string &value)
    {
        auto low = [](char c) {
            return char(std::tolower(static_cast<unsigned char>(c)));
        };
        size_t p = 0, v = 0;
        size_t star = std::string::npos, mark = 0;
        while (v < value.size()) {
            if (p < pattern.size()
                    && (pattern[p] == '?'
                        || low(pattern[p]) == low(value[v]))) {
                ++p; ++v;
            }
            else if (p < pattern.size() && pattern[p] == '*') {
                star = p++;
                mark = v;
            }
            else if (star != std::string::npos) {
                p = star + 1;
                v = ++mark;
            }
            else {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == '*')
            ++p;
        return p == pattern.size();
    }

    static bool patternMatches(const std::string &pattern,
                               const std::string &value)
    {
        if (pattern.empty() || pattern == "*")
            return true;
        return globMatch(pattern, value);
    }

    /// How specific a pattern is, for choosing between grants that
    /// both match: a literal beats a partial wildcard beats `*`. Kept
    /// identical to the sharing UI's rule scoring.
    static int specificityOf(const std::string &pattern)
    {
        if (pattern.empty() || pattern == "*")
            return 0;
        int literal = 0;
        bool wild = false;
        for (char c : pattern) {
            if (c == '*' || c == '?')
                wild = true;
            else
                ++literal;
        }
        return (wild ? 1 : 4096) + literal;
    }

    /// The address grants match against: without the ephemeral source
    /// port, which differs on every visit. Only a v4 `ip:port` carries
    /// one this way — an IPv6 address is all colons and stays whole.
    static std::string portlessAddress(const std::string &address)
    {
        auto colon = address.rfind(':');
        if (colon == std::string::npos || colon == 0)
            return address;
        bool digits = colon + 1 < address.size();
        for (size_t i = colon + 1; i < address.size(); ++i) {
            digits = digits
                && std::isdigit(static_cast<unsigned char>(address[i]));
        }
        if (!digits
                || std::count(address.begin(), address.end(), ':') != 1)
            return address;
        return address.substr(0, colon);
    }

    /// The door's answer for one presentation.
    struct Judgement {
        bool admitted = false;
        bool viewOnly = false;
        uint64_t grant = 0;   ///< admitting grant id; 0 = legacy door
    };

    /// Judge a presentation against a grant list (docs/ShareAccess.md
    /// §2): the most specific matching grant decides, and no match —
    /// or a banned best match — refuses.
    static Judgement judgeWith(const std::vector<SceneGrant> &list,
                               const std::string &token,
                               const std::string &identity,
                               const std::string &client,
                               const std::string &address)
    {
        Judgement out;
        const std::string addr = portlessAddress(address);
        const SceneGrant *best = nullptr;
        int64_t bestScore = -1;
        for (const auto &g : list) {
            if (!g.token.empty() && g.token != token)
                continue;
            if (!patternMatches(g.identity, identity)
                    || !patternMatches(g.client, client)
                    || !patternMatches(g.address, addr))
                continue;
            // Identity outranks name outranks address: the verified
            // part first, then what a person chose, then where they
            // happen to be. The token is a filter, not a rank — a ban
            // must not be outranked by the invitation it revokes.
            int64_t score = (int64_t(specificityOf(g.identity)) * 8192
                             + specificityOf(g.client)) * 8192
                            + specificityOf(g.address);
            if (score > bestScore) {
                bestScore = score;
                best = &g;
            }
        }
        if (!best || best->access == 2)
            return out;
        out.admitted = true;
        out.viewOnly = best->access == 1;
        out.grant = best->id;
        return out;
    }

    /// The door: the grant list when one is set, else the legacy
    /// shared token (SceneServer.h, setGrants).
    Judgement judge(const std::string &token, const std::string &identity,
                    const std::string &client, const std::string &address)
    {
        std::lock_guard<std::mutex> guard(tokenMutex);
        if (grantList.empty()) {
            Judgement out;
            out.admitted = tokenSecret.empty() || token == tokenSecret;
            return out;
        }
        return judgeWith(grantList, token, identity, client, address);
    }

    bool grantsActive()
    {
        std::lock_guard<std::mutex> guard(tokenMutex);
        return !grantList.empty();
    }

    /// What each of the last few publishes changed, oldest first and
    /// consecutive. A viewer that missed some is caught up by merging
    /// the ones it missed; one that fell out of this window gets the
    /// payload whole, so the depth is a bandwidth choice and never a
    /// correctness one.
    struct Change {
        uint64_t version = 0;
        std::vector<SceneSnapshot::ObjectEntry> changed;
        std::vector<uint64_t> removed;
    };
    static const size_t kHistory = 32;

    /// Everything the server holds *per served document*
    /// (docs/MultiDocServe.md §3): a second document is a second map
    /// entry, not a fight over singleton slots, and connections join
    /// one by name on the wire (§4). Guarded by \a mutex except where
    /// a member says otherwise.
    struct DocGroup {
        /// The document this group serves; empty for the anonymous
        /// group the FC_BGFX_SERVE_SCENE viewer path publishes into,
        /// which no name on the wire can join.
        std::string name;
        /// The display label the `docs` listing shows for this
        /// document (docs/MultiDocServe.md §4). Set by
        /// setDocumentInfo; the name is the wire key, this is for
        /// humans.
        std::string label;
        /// Objects in the currently served payload, from the last
        /// publish — the listing's rough size cue.
        size_t objects = 0;
        /// Joinable by name on the wire: set while a source stands
        /// behind this group (setDocumentInfo .. releaseGroup). The
        /// anonymous group is never live — an unadorned join falls
        /// back to it only when nothing is (defaultJoin).
        bool live = false;
        /// Whether this group was ever served — what tells a
        /// connection's loop that a group it is joined to was torn
        /// down (live dropped) rather than never named at all.
        bool wasServed = false;
        /// Serve order, for picking the default: the first-served
        /// document still alive is what an unadorned hello joins.
        uint64_t serveSeq = 0;

        std::vector<uint8_t> payload;
        /// The version \a payload was built for; both change together,
        /// in publish(). \a handedOut runs ahead of it — a version is
        /// claimed before the payload that carries it is serialized.
        uint64_t version = 0;
        uint64_t handedOut = 0;
        /// Where \a payload's object list sits, so it can be narrowed
        /// for a viewer that is behind (SceneDump.h, spliceObjectDelta).
        SceneSnapshot::RootSpans spans;
        std::deque<Change> history;
        /// This run of this document's stream, and who is numbering its
        /// versions (SceneServer.h, beginPublish).
        uint64_t session = 0;
        const void *publisher = nullptr;

        /// Blob keys named by this group's publish in flight, then by
        /// its last two publishes. The store itself is global (blobs
        /// are content-keyed and immutable, so documents share them);
        /// which keys are *alive* is a per-document fact, and a key
        /// retires only when no group at all names it (retireBlobs).
        std::set<std::string> pendingKeys, currentKeys, previousKeys;

        /// Level bookkeeping (docs/SceneStreaming.md §7): rung demand
        /// is driven by the viewers of this document's scene.
        std::set<std::pair<std::string, uint32_t>> levelAsked;
        std::map<std::pair<std::string, uint32_t>,
                 std::pair<std::string, uint32_t>> levelBuilt;
        size_t levelsDone = 0;

        /// This document's publisher-side consumers. Guarded by
        /// \a handlerMutex, not \a mutex — a handler runs arbitrary
        /// marshaling code and must not be looked up under the payload
        /// lock.
        std::function<void(const ScenePickRequest &)> pickHandler;
        std::function<void(SceneControlRequest &&)> controlHandler;
        std::function<void()> workNotifier;

        uint64_t ensureSession()
        {
            if (!session) {
                // Wall clock, so that two runs of this process — which
                // is exactly the case the id exists for — cannot
                // collide the way a counter or an address could.
                session = uint64_t(std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                            std::chrono::system_clock::now()
                                .time_since_epoch())
                        .count());
                if (!session)
                    session = 1;   // 0 means "unknown" on the wire
            }
            return session;
        }
    };

    /// The served documents, keyed by document name. std::map for node
    /// stability: connections and level jobs hold DocGroup pointers.
    /// Entries are never erased — releaseGroup() takes one off the
    /// wire instead — so those pointers cannot dangle; each released
    /// group's connections re-home themselves on their own loop tick
    /// (wsLoopBody).
    std::map<std::string, DocGroup> groups;
    /// The group an unadorned client means: the first one anything
    /// created — for Gui.serveDocument that is the first served
    /// document, because serve() builds the source (which claims its
    /// group) before it starts the listener; for the FC_BGFX_SERVE_SCENE
    /// path it is the anonymous group the viewer's renderer publishes
    /// into.
    DocGroup *defaultGrp = nullptr;

    /// The group named \a name, created on first use; empty names the
    /// default group. Call with \a mutex held.
    DocGroup &group(const std::string &name)
    {
        if (name.empty())
            return defaultGroup();
        auto it = groups.find(name);
        if (it == groups.end()) {
            it = groups.emplace(name, DocGroup()).first;
            it->second.name = name;
            if (!defaultGrp)
                defaultGrp = &it->second;
        }
        return it->second;
    }

    /// The default group, created (anonymous) on first use. Call with
    /// \a mutex held.
    DocGroup &defaultGroup()
    {
        if (!defaultGrp) {
            defaultGrp = &groups[std::string()];
        }
        return *defaultGrp;
    }

    /// Serve-order stamps for the live groups (setDocumentInfo).
    uint64_t serveSeqCounter = 0;

    /// The group an unadorned *join* means (docs/MultiDocServe.md §4):
    /// the first-served document still alive, else the anonymous group
    /// a viewer-path publisher may be feeding, else nothing. Distinct
    /// from defaultGrp, which resolves the *publisher-side* unnamed
    /// calls and never re-points. Call with \a mutex held.
    DocGroup *defaultJoin()
    {
        DocGroup *best = nullptr;
        for (auto &entry : groups) {
            DocGroup &g = entry.second;
            if (g.live && (!best || g.serveSeq < best->serveSeq))
                best = &g;
        }
        if (best)
            return best;
        auto it = groups.find(std::string());
        return it == groups.end() ? nullptr : &it->second;
    }

    /// The live group named \a name, or the default join for an empty
    /// name; null when there is no such document to join. Never
    /// creates a group: an unknown name on the wire is an error, not a
    /// group (§4). Call with \a mutex held.
    DocGroup *joinable(const std::string &name)
    {
        if (name.empty())
            return defaultJoin();
        auto it = groups.find(name);
        return it != groups.end() && it->second.live ? &it->second : nullptr;
    }

    /// Escape \a in for a JSON string literal: document names are
    /// tame, but labels are user text.
    static std::string jsonEscape(const std::string &in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            }
            else if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned char>(c));
                out += buf;
            }
            else {
                out += c;
            }
        }
        return out;
    }

    /// The document listing, `{"cmd":"docs",...}` — the reply to the
    /// `docs` verb and the unsolicited push a serve or unserve sends
    /// (docs/MultiDocServe.md §4). Call with \a mutex held.
    std::string docsJsonLocked()
    {
        std::vector<const DocGroup *> live;
        for (auto &entry : groups) {
            if (entry.second.live)
                live.push_back(&entry.second);
        }
        std::sort(live.begin(), live.end(),
                  [](const DocGroup *a, const DocGroup *b) {
                      return a->serveSeq < b->serveSeq;
                  });
        std::string out = "{\"cmd\":\"docs\",\"list\":[";
        for (size_t i = 0; i < live.size(); ++i) {
            if (i)
                out += ',';
            out += "{\"name\":\"" + jsonEscape(live[i]->name)
                + "\",\"label\":\"" + jsonEscape(live[i]->label)
                + "\",\"objects\":" + std::to_string(live[i]->objects) + "}";
        }
        out += "],\"default\":\"";
        if (const DocGroup *d = defaultJoin())
            out += jsonEscape(d->name);
        out += "\"}";
        return out;
    }

    /// Push the current document listing to every connected viewer —
    /// what their document menus redraw from. Caller holds no lock.
    void pushDocs()
    {
        std::string json;
        {
            std::lock_guard<std::mutex> guard(mutex);
            json = docsJsonLocked();
        }
        broadcastControl(json);
    }

    /// The payload of \a g for a viewer holding \a held: the
    /// difference since it, when the history reaches back that far,
    /// and the whole root otherwise. Empty when there is nothing to
    /// send. Call with \a mutex held.
    ///
    /// The merge is last-wins per objectKey, which is what makes this
    /// safe to serve: the last thing said about an object is its
    /// current state, so the result names only keys the publish in
    /// flight still names — never a chunk that has since been retired.
    std::vector<uint8_t> payloadFor(DocGroup &g, uint64_t held)
    {
        if (g.payload.empty() || held == g.version)
            return {};
        // No manifest layout (nothing to narrow), or a viewer holding
        // nothing, or one whose version predates what we remember.
        if (!g.spans.listEnd || !held || held > g.version
                || g.history.empty()
                || g.history.front().version > held + 1)
            return g.payload;

        std::map<uint64_t, const SceneSnapshot::ObjectEntry *> changed;
        std::set<uint64_t> removed;
        for (const auto &h : g.history) {
            if (h.version <= held)
                continue;
            for (const auto &e : h.changed) {
                changed[e.objectKey] = &e;
                removed.erase(e.objectKey);
            }
            for (uint64_t key : h.removed) {
                removed.insert(key);
                changed.erase(key);
            }
        }

        std::vector<SceneSnapshot::ObjectEntry> entries;
        entries.reserve(changed.size());
        for (const auto &item : changed)
            entries.push_back(*item.second);
        std::vector<uint64_t> gone(removed.begin(), removed.end());

        std::vector<uint8_t> out;
        // The changed manifests ride inline (v37): they are new by
        // definition, so no viewer cache ever answers for them, and the
        // store is right here. Held under \a mutex like everything else
        // in this call.
        if (!spliceObjectDelta(
                g.payload, g.spans, held, entries, gone, out,
                [this](const std::string &key)
                    -> const std::vector<uint8_t> * {
                    auto it = blobs.find(key);
                    return it == blobs.end() ? nullptr : &it->second;
                }))
            return g.payload;
        return out;
    }

    /// Out-of-band texture payloads, addressed by content key and
    /// served over GET /blob?key= — the snapshot only names them
    /// (SceneDump.h, v26). Immutable by construction, so a viewer may
    /// cache one forever — and documents may share one, which is why
    /// the store is global while the key *liveness* sets live in the
    /// groups.
    std::map<std::string, std::vector<uint8_t>> blobs;

    /// Bounds on one POST /blobs request, which is otherwise an
    /// unauthenticated allocation the client controls.
    static const size_t kMaxBatchKeys = 512;
    static const size_t kMaxBatchBody = kMaxBatchKeys * 64;

    void addBlob(DocGroup &g, const std::string &key,
                 std::vector<uint8_t> &&data)
    {
        std::lock_guard<std::mutex> guard(mutex);
        g.pendingKeys.insert(key);
        // Content addressed: an existing entry is already this payload.
        blobs.emplace(key, std::move(data));
    }

    /// Name a stored blob as still in use by the publish in flight,
    /// without re-sending its bytes — what lets the publisher answer
    /// from its cacheId memo instead of re-serializing a mesh. Returns
    /// false when the blob is gone, which is the memo's invalidation:
    /// the caller then rebuilds and stores it.
    bool retainBlob(DocGroup &g, const std::string &key, uint32_t *size)
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = blobs.find(key);
        if (it == blobs.end())
            return false;
        g.pendingKeys.insert(key);
        if (size)
            *size = uint32_t(it->second.size());
        return true;
    }

    /// Roll \a g's generations and drop from the store what no group
    /// names at all — pending, current or previous: another document's
    /// publish in flight must not lose its blobs to this one's
    /// retirement. Called with \a mutex held, from publish().
    void retireBlobs(DocGroup &g)
    {
        g.previousKeys = std::move(g.currentKeys);
        g.currentKeys = std::move(g.pendingKeys);
        g.pendingKeys.clear();
        auto liveAnywhere = [this](const std::string &key) {
            for (const auto &entry : groups) {
                const DocGroup &grp = entry.second;
                if (grp.pendingKeys.count(key) || grp.currentKeys.count(key)
                        || grp.previousKeys.count(key))
                    return true;
            }
            return false;
        };
        for (auto it = blobs.begin(); it != blobs.end();) {
            if (liveAnywhere(it->first))
                ++it;
            else
                it = blobs.erase(it);
        }
        // A built level whose chunk got rolled away is a memo pointing
        // at nothing — the mesh it was made from left the scene, and
        // no manifest retained it. Forget it entirely, so a viewer
        // that wants it again may ask again. Every group is swept: the
        // erase above is global, so another group's memo may have just
        // lost its chunk too.
        for (auto &entry : groups) {
            DocGroup &grp = entry.second;
            for (auto it = grp.levelBuilt.begin();
                 it != grp.levelBuilt.end();) {
                if (blobs.count(it->second.first)) {
                    ++it;
                }
                else {
                    grp.levelAsked.erase(it->first);
                    it = grp.levelBuilt.erase(it);
                }
            }
        }
    }

    /// The level-generation work queue (docs/SceneStreaming.md §7,
    /// phase 5c). This is the one place the server stops answering
    /// from its store alone: a declared level has no bytes, so asking
    /// for it is asking for *work*, and the work has to be queued,
    /// done off the connection threads, and announced by the next
    /// publish rather than by the reply. All of it stays content-keyed
    /// and idempotent — every viewer wanting the same level is the
    /// same request — which is what invariant 7 keeps.
    struct LevelJob {
        std::string source;
        uint32_t level = 0;
        /// Whose demand this is: the memo, the announcement and the
        /// wake-up all belong to one document's stream. Stable because
        /// groups are map nodes and nothing removes one yet; group
        /// teardown (stage 3b) must purge its jobs from this queue.
        DocGroup *group = nullptr;
    };
    std::deque<LevelJob> levelQueue;      ///< guarded by \a mutex
    /// (levelAsked: every (source, level) ever accepted per group, so
    /// a repeat is free — an entry leaves only when its answer does
    /// (retireBlobs) or its source turns out to be gone by the time
    /// the job runs. levelBuilt: (source, level) -> (key, size) of the
    /// generated chunk, what the publisher's MeshBlobSink::built
    /// consults. Both live in DocGroup.)
    int levelThreads = 0;                 ///< spawned so far, ≤ cap
    std::condition_variable levelCv;      ///< pairs with \a mutex

    /// The LevelThreads render parameter, pushed down by the Gui layer
    /// (SceneStreamServer::setLevelThreadCap); 0 = auto.
    static inline std::atomic<int> s_levelThreadParam{0};

    /// How many level builds may run at once. A build is a pure
    /// function of (shape, params, source chunk) — MeshSource.h — so
    /// this is plain CPU fan-out; auto-sized modestly by default,
    /// because BRepMesh already parallelizes each build internally
    /// over OCCT's shared thread pool. The LevelThreads render
    /// parameter sets it (0 = auto); FC_LEVEL_THREADS overrides.
    /// Consulted per spawn, so a parameter change applies to workers
    /// not yet started.
    static int levelThreadCap()
    {
        static const int envCap = [] {
            const char *env = std::getenv("FC_LEVEL_THREADS");
            return env ? std::atoi(env) : 0;
        }();
        int n = envCap > 0 ? envCap : s_levelThreadParam.load();
        if (n > 0)
            return std::min(n, 64);
        static const int autoCap = [] {
            unsigned hw = std::thread::hardware_concurrency();
            return int(std::max(1u, std::min(4u, hw / 4)));
        }();
        return autoCap;
    }

    /// Accept a request to build \a level of the mesh whose exact
    /// chunk is stored under \a source, on behalf of \a g's viewers.
    /// False when the source is not a chunk this server holds —
    /// nothing could be generated from it.
    bool requestLevel(DocGroup &g, const std::string &reqSource,
                      uint32_t level)
    {
        // The ladder never declares more than a handful of levels, and
        // the request is unauthenticated: an absurd level is a bad
        // request, not work. The exact rung of a coarse-first ladder
        // rides its sentinel index (§7).
        if (level >= 16 && level != kExactMeshLevel)
            return false;
        // Any built rung of a ladder may name the source; the job —
        // its dedup, its memo, the announcement lookup — lives under
        // the canonical (published) key, which is the one the
        // serializer consults built() with.
        const std::string source =
            MeshSourceRegistry::instance().canonical(reqSource);
        std::lock_guard<std::mutex> guard(mutex);
        if (!blobs.count(source))
            return false;
        // Already queued, running, built, or refused for good: all the
        // same answer, because the announcement never was the reply.
        if (!g.levelAsked.emplace(source, level).second)
            return true;
        levelQueue.push_back({source, level, &g});
        // One more thread per accepted job until the cap fills: a big
        // publish's burst of asks fans out immediately, and the
        // threads are process-lifetime cv-waiters afterwards, like
        // the accept loop.
        if (levelThreads < levelThreadCap()) {
            ++levelThreads;
            std::thread([this]() { levelLoop(); }).detach();
        }
        levelCv.notify_one();
        return true;
    }

    /// A level thread: up to levelThreadCap() of them, each
    /// process-lifetime, like the accept loop. levelAsked admits every
    /// (source, level) once, so no two threads ever hold the same job,
    /// and the build itself is pure — concurrency needs nothing beyond
    /// the queue mutex.
    void levelLoop()
    {
        for (;;) {
            LevelJob job;
            std::vector<uint8_t> source;
            {
                std::unique_lock<std::mutex> lock(mutex);
                levelCv.wait(lock, [this] { return !levelQueue.empty(); });
                job = std::move(levelQueue.front());
                levelQueue.pop_front();
                auto it = blobs.find(job.source);
                if (it == blobs.end()) {
                    // The scene moved on before the job ran. Forget
                    // the ask, so a republish naming the mesh again
                    // starts clean.
                    job.group->levelAsked.erase({job.source, job.level});
                    continue;
                }
                // Copied, so generation runs outside the lock: a
                // decimation is milliseconds, and every connection
                // thread shares this mutex.
                source = it->second;
            }
            // A shape-backed source re-tessellates at the level's
            // deviation — edge polylines land on the coarse surface by
            // construction (MeshSource.h). Decimation is the fallback
            // for meshes no shape claims (mesh objects, bundled
            // captures) or whose generator refused.
            std::vector<uint8_t> chunk;
            const char *how = "retess";
            if (!MeshSourceRegistry::instance().generate(
                        job.source, job.level, source.data(), source.size(),
                        chunk)) {
                how = "decimate";
                if (!generateMeshLevel(source.data(), source.size(),
                                       job.level, chunk)) {
                    // Nothing to simplify at this level, or not a mesh
                    // chunk at all. The level stays declared-unbuilt,
                    // and the ask stays recorded so it is not retried
                    // forever.
                    continue;
                }
            }
            std::string key = sha1Hex(chunk.data(), chunk.size());
            uint32_t size = uint32_t(chunk.size());
            std::fprintf(stderr,
                         "scene server: built level %u of %s -> %s "
                         "(%u of %zu bytes, %s)\n",
                         job.level, job.source.c_str(), key.c_str(), size,
                         source.size(), how);
            {
                std::lock_guard<std::mutex> guard(mutex);
                // Straight into the pending generation: the publish
                // this triggers is the one that names it, and until
                // then the roll must not sweep it away.
                job.group->pendingKeys.insert(key);
                blobs.emplace(key, std::move(chunk));
                job.group->levelBuilt[{job.source, job.level}] = {key, size};
                ++job.group->levelsDone;
            }
            // The publisher's poll lives in the render path; wake it,
            // or an idle backend announces nothing until something
            // else happens to want a frame.
            notifyWork(*job.group);
        }
    }

    /// The key a generated level is stored under, or empty while
    /// nobody has built it. Retains the chunk for the publish in
    /// flight, exactly like retainBlob — being written into a manifest
    /// is what keeps it alive from here on.
    std::string builtLevel(DocGroup &g, const std::string &source,
                           uint32_t level, uint32_t *size)
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = g.levelBuilt.find({source, level});
        if (it == g.levelBuilt.end())
            return {};
        if (!blobs.count(it->second.first)) {
            // Rolled away between publishes; forget it (see
            // retireBlobs) rather than announce a key a fetch would
            // 404 on.
            g.levelAsked.erase(it->first);
            g.levelBuilt.erase(it);
            return {};
        }
        g.pendingKeys.insert(it->second.first);
        if (size)
            *size = it->second.second;
        return it->second.first;
    }

    size_t levelsBuilt(DocGroup &g)
    {
        std::lock_guard<std::mutex> guard(mutex);
        return g.levelsDone;
    }


    /// Guards every group's handler slots (pickHandler, controlHandler,
    /// workNotifier) — separate from \a mutex so a handler lookup never
    /// contends with the payload lock.
    std::mutex handlerMutex;

    /// Ask \a g's owner for a frame. Everything the render path polls
    /// -- the publish trigger, and whether animated content still has
    /// an audience -- only gets looked at when a frame happens, so a
    /// server thread that changes any of it has to say so.
    void notifyWork(DocGroup &g)
    {
        std::function<void()> notify;
        {
            std::lock_guard<std::mutex> guard(handlerMutex);
            notify = g.workNotifier;
        }
        if (notify)
            notify();
    }

    /// One live WebSocket connection, registered by its wsLoop. All
    /// sends stay on that loop's thread: control messages are queued
    /// here and drained by the loop within its poll interval.
    struct Conn {
        int fd = -1;
        /// Stable identity for cross-thread reply routing (the Conn
        /// itself is stack-owned by its wsLoop): a control reply looks
        /// the connection up by id under connMutex and is dropped when
        /// it is gone. Never reused within a run.
        uint64_t id = 0;
        /// The scene version this connection has been given, seeded
        /// with what the client said it held on the upgrade request.
        /// Touched only by this connection's own push loop.
        uint64_t sent = 0;
        /// The document group this connection is joined to, or null
        /// when it is joined to nothing (a hello naming an unknown
        /// document, or the last served document going away). Touched
        /// only on this connection's own loop thread — joins, switches
        /// and the re-home after a teardown all happen there.
        DocGroup *group = nullptr;
        std::vector<std::string> pendingText; ///< queued control JSONs
        bool viewer = false;   ///< sent a hello — answers control requests
        std::string build;     ///< bundle build stamp from the hello
        /// Display label from the hello (docs/MultiDocServe.md §4):
        /// names this connection in the decisions journal and the
        /// sharing roster. Never parsed, never trusted.
        std::string client;
        /// Peer address and port, for the roster. The port is what
        /// tells two connections apart when they share an address —
        /// which every tunnelled viewer does (they all arrive as the
        /// tunnel's local end).
        std::string addr;
        /// What a trusted proxy said the client's address is
        /// (X-Forwarded-For, setTrustProxy); empty when nothing did.
        std::string fwd;
        /// The verified identity a trusted front door asserted on the
        /// upgrade (assertedIdentity); empty when none did. Fixed for
        /// the connection's life — unlike the client label, it cannot
        /// arrive late, because it rides the upgrade request itself.
        std::string identity;
        /// The address the door judges this connection by, portless:
        /// the forwarded address when a trusted proxy stated one, else
        /// the socket peer's ip. Fixed for the connection's life.
        std::string matchAddr;
        /// The invitation secret this connection presented — `?token=`
        /// on the upgrade, replaced by the hello's if that carries
        /// one. Guarded by connMutex: re-judging a live-list change
        /// reads it from the GUI thread.
        std::string presentedToken;
        /// The grant that admitted this connection (SceneGrant::id, 0
        /// under the legacy door). Guarded by connMutex, same reason.
        uint64_t grant = 0;
        /// Mirror of \a authorized under connMutex (like docName), so
        /// the re-judge on a grant-list change can see which
        /// connections are in. \a authorized itself stays owner-thread
        /// -only; a re-judge never opens the door for a waiting
        /// connection, only closes it (kicked) or changes its access —
        /// admission happens on the connection's own hello.
        bool admitted = false;
        /// The joined document's name, mirrored under connMutex for
        /// the roster — \a group itself is owner-thread-only.
        std::string docName;
        /// When this connection registered.
        std::chrono::steady_clock::time_point since;
        /// Passed the door (docs/MultiDocServe.md §8): no token
        /// configured, the upgrade carried it, or the hello did. Until
        /// then no payload is pushed and no verb but the hello works.
        /// Owner-thread-only, like group.
        bool authorized = true;
        /// View-only (guarded by connMutex, the host flips it from the
        /// GUI thread): picks dropped, mutating ops refused.
        bool viewOnly = false;
        /// The host asked this connection closed (guarded by
        /// connMutex); its own loop tells it and hangs up.
        bool kicked = false;
        /// The stamp a reload was already pushed for — one push per
        /// bundle generation, no loops.
        std::string reloadPushed;
        /// WebSocket fragmentation reassembly (a browser may split a
        /// large dumpFrame upload into continuation frames).
        std::string fragData;
        uint8_t fragOpcode = 0;
    };
    std::mutex connMutex;
    std::vector<Conn *> conns;
    uint64_t connIdCounter = 0;   ///< guarded by connMutex
    std::condition_variable dumpCv;    ///< guarded by connMutex
    /// In-flight dumpFrame collection (one at a time).
    struct DumpCollect {
        uint32_t id = 0;
        size_t expected = 0;
        std::vector<ViewerFrameDump> dumps;
    };
    DumpCollect *dumpCollect = nullptr;
    uint32_t dumpIdCounter = 0;
    /// In-flight decision-log collection (one at a time), the same
    /// shape as the frame dumps: broadcast a control request, wait for
    /// each viewer's 'L' answer (docs/SceneStreaming.md §7 — the
    /// viewer keeps its plan/fetch journal locally, and this is how it
    /// is pulled).
    struct LogCollect {
        uint32_t id = 0;
        size_t expected = 0;
        std::vector<std::string> logs;
    };
    LogCollect *logCollect = nullptr;

    void broadcastControl(const std::string &json)
    {
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns) {
            if (conn->viewer)
                conn->pendingText.push_back(json);
        }
    }

    /// The sharing UI's roster-changed cue. Guarded by handlerMutex
    /// like the group handlers; invoked with no lock held.
    std::function<void()> clientsChanged;

    void notifyClientsChanged()
    {
        std::function<void()> notify;
        {
            std::lock_guard<std::mutex> guard(handlerMutex);
            notify = clientsChanged;
        }
        if (notify)
            notify();
    }

    /// Mint the live-only easing for a renamed connection
    /// (docs/ShareAccess.md §2): accept the new name on the same
    /// token, bounded like the invitation it eases — the admitting
    /// grant's identity and address patterns where it still exists,
    /// the connection's own verified identity otherwise. Never wider
    /// than what already got in.
    void addEasing(Conn &conn, const std::string &token,
                   const std::string &newName, uint64_t fromGrant)
    {
        SceneGrant g;
        g.token = token;
        g.client = newName;
        g.identity = conn.identity;
        g.liveOnly = true;
        {
            std::lock_guard<std::mutex> guard(connMutex);
            g.access = conn.viewOnly ? 1 : 0;
        }
        {
            std::lock_guard<std::mutex> guard(tokenMutex);
            for (const auto &old : grantList) {
                if (old.id == fromGrant) {
                    g.identity = old.identity;
                    g.address = old.address;
                    break;
                }
            }
            g.id = ++grantIdCounter;
            grantList.push_back(g);
        }
        std::lock_guard<std::mutex> guard(connMutex);
        conn.grant = g.id;
    }

    /// Re-judge every admitted connection after the live grant list
    /// changed (SceneServer.h, setGrants): one no grant now admits is
    /// told and closed — the enforcement that used to live in the
    /// desktop roster poll, moved into the door — and one a different
    /// grant admits gets that grant's access. Never *opens* the door:
    /// an unauthorized connection is admitted only by its own hello,
    /// which carries what this cannot know is coming.
    void rejudgeConnections()
    {
        std::vector<SceneGrant> list;
        std::string secret;
        {
            std::lock_guard<std::mutex> guard(tokenMutex);
            list = grantList;
            secret = tokenSecret;
        }
        bool changed = false;
        {
            std::lock_guard<std::mutex> guard(connMutex);
            for (Conn *conn : conns) {
                if (!conn->admitted || conn->kicked)
                    continue;
                Judgement entry;
                if (list.empty()) {
                    entry.admitted = secret.empty()
                        || conn->presentedToken == secret;
                }
                else {
                    entry = judgeWith(list, conn->presentedToken,
                                      conn->identity, conn->client,
                                      conn->matchAddr);
                }
                if (!entry.admitted) {
                    conn->pendingText.push_back(
                        "{\"cmd\":\"error\",\"code\":\"Refused\"}");
                    conn->kicked = true;
                    changed = true;
                }
                else {
                    if (!list.empty()
                            && conn->viewOnly != entry.viewOnly) {
                        conn->viewOnly = entry.viewOnly;
                        changed = true;
                    }
                    conn->grant = entry.grant;
                }
            }
        }
        if (changed)
            notifyClientsChanged();
    }

    int clients(std::vector<SceneClientInfo> &out)
    {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> guard(connMutex);
        out.clear();
        out.reserve(conns.size());
        for (Conn *conn : conns) {
            SceneClientInfo info;
            info.id = conn->id;
            info.client = conn->client;
            info.doc = conn->docName;
            info.peer = conn->addr;
            info.proxied = !conn->fwd.empty();
            info.address = info.proxied ? conn->fwd : conn->addr;
            info.identity = conn->identity;
            info.grant = conn->grant;
            info.viewer = conn->viewer;
            info.viewOnly = conn->viewOnly;
            info.connectedMs = uint64_t(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - conn->since).count());
            out.push_back(std::move(info));
        }
        return int(out.size());
    }

    bool setClientViewOnly(uint64_t id, bool viewOnly)
    {
        bool found = false;
        {
            std::lock_guard<std::mutex> guard(connMutex);
            for (Conn *conn : conns) {
                if (conn->id == id) {
                    conn->viewOnly = viewOnly;
                    // Tell the client its mode, so its UI can say so.
                    conn->pendingText.push_back(
                        viewOnly
                            ? "{\"cmd\":\"config\",\"viewOnly\":true}"
                            : "{\"cmd\":\"config\",\"viewOnly\":false}");
                    found = true;
                    break;
                }
            }
        }
        if (found)
            notifyClientsChanged();
        return found;
    }

    bool kickClient(uint64_t id)
    {
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns) {
            if (conn->id == id) {
                conn->kicked = true;
                return true;
            }
        }
        return false;
    }

    /// The one gate every join goes through — hello and switch alike
    /// (docs/MultiDocServe.md §4), and the choke point a later
    /// per-connection ACL would occupy. An empty \a name means the
    /// default document. False when there is no such document to join,
    /// leaving the connection where it was; on a join that changes
    /// groups the connection's version resets, so the next push is the
    /// new document's full snapshot. Must run on \a conn's own loop
    /// thread, the only one allowed to touch its group.
    bool joinDocument(Conn &conn, const std::string &name)
    {
        {
            std::lock_guard<std::mutex> guard(mutex);
            DocGroup *g = joinable(name);
            if (!g)
                return false;
            if (g != conn.group) {
                conn.group = g;
                conn.sent = 0;
            }
        }
        // Mirror the joined name for the roster — group itself is
        // owner-thread-only, and this IS the owner thread.
        {
            std::lock_guard<std::mutex> guard(connMutex);
            conn.docName = conn.group->name;
        }
        notifyClientsChanged();
        return true;
    }

    int requestFrameDumps(int mode, int timeoutMs,
                          std::vector<ViewerFrameDump> &out)
    {
        std::unique_lock<std::mutex> lock(connMutex);
        if (dumpCollect)
            return 0;   // another collection still in flight
        size_t expected = 0;
        for (Conn *conn : conns) {
            if (conn->viewer)
                ++expected;
        }
        if (!expected)
            return 0;
        DumpCollect collect;
        collect.id = ++dumpIdCounter;
        collect.expected = expected;
        dumpCollect = &collect;
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "{\"cmd\":\"dumpFrame\",\"id\":%u,\"mode\":%d}",
                      collect.id, mode);
        for (Conn *conn : conns) {
            if (conn->viewer)
                conn->pendingText.push_back(msg);
        }
        dumpCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                        [&collect]() {
                            return collect.dumps.size() >= collect.expected;
                        });
        dumpCollect = nullptr;
        out = std::move(collect.dumps);
        return int(out.size());
    }

    int requestDecisionLogs(int timeoutMs, std::vector<std::string> &out)
    {
        std::unique_lock<std::mutex> lock(connMutex);
        if (logCollect)
            return 0;
        size_t expected = 0;
        for (Conn *conn : conns) {
            if (conn->viewer)
                ++expected;
        }
        if (!expected)
            return 0;
        LogCollect collect;
        collect.id = ++dumpIdCounter;
        collect.expected = expected;
        logCollect = &collect;
        char msg[64];
        std::snprintf(msg, sizeof(msg),
                      "{\"cmd\":\"dumpDecisions\",\"id\":%u}", collect.id);
        for (Conn *conn : conns) {
            if (conn->viewer)
                conn->pendingText.push_back(msg);
        }
        dumpCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                        [&collect]() {
                            return collect.logs.size() >= collect.expected;
                        });
        logCollect = nullptr;
        out = std::move(collect.logs);
        return int(out.size());
    }

    /// Minimal JSON field extraction for the tiny, self-generated
    /// control messages (no JSON dependency in this lib).
    static bool jsonInt(const std::string &json, const char *key,
                        long long &out)
    {
        std::string needle = std::string("\"") + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos)
            return false;
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos)
            return false;
        out = std::strtoll(json.c_str() + pos + 1, nullptr, 10);
        return true;
    }

    static bool jsonStr(const std::string &json, const char *key,
                        std::string &out)
    {
        std::string needle = std::string("\"") + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos)
            return false;
        pos = json.find(':', pos + needle.size());
        auto open = pos == std::string::npos
            ? std::string::npos : json.find('"', pos + 1);
        auto close = open == std::string::npos
            ? std::string::npos : json.find('"', open + 1);
        if (close == std::string::npos)
            return false;
        out = json.substr(open + 1, close - open - 1);
        return true;
    }

    /// Expected viewer bundle build stamp: the fcviewer.stamp file the
    /// WASM build writes next to the bundle (see wasm/stamp.cmake),
    /// located via FC_BGFX_VIEWER_BUILD. Cached by mtime so the
    /// periodic per-connection checks stay cheap; empty = feature off
    /// (env unset or no stamp file). A rebuild while this backend is
    /// serving changes the file, and connected viewers get their
    /// reload within a poll interval.
    std::mutex stampMutex;
    std::string stampCache;
    time_t stampMtime = 0;
    std::string viewerStamp()
    {
        static const char *dir = std::getenv("FC_BGFX_VIEWER_BUILD");
        if (!dir || !*dir)
            return std::string();
        std::string path = std::string(dir) + "/fcviewer.stamp";
        std::lock_guard<std::mutex> guard(stampMutex);
        struct stat st;
        if (::stat(path.c_str(), &st) != 0) {
            stampCache.clear();
            stampMtime = 0;
            return stampCache;
        }
        if (st.st_mtime == stampMtime)
            return stampCache;
        stampCache.clear();
        if (std::FILE *f = std::fopen(path.c_str(), "rb")) {
            char buf[64];
            size_t n = std::fread(buf, 1, sizeof(buf), f);
            std::fclose(f);
            for (size_t i = 0; i < n; ++i)
                if (std::isalnum(static_cast<unsigned char>(buf[i])))
                    stampCache += buf[i];
        }
        stampMtime = st.st_mtime;
        return stampCache;
    }

    /// Queue a cache-busting reload for a viewer whose reported bundle
    /// build no longer matches the on-disk stamp (once per stamp; the
    /// page-side bust-parameter guard also refuses repeats). Caller
    /// holds no lock.
    void pushReloadIfStale(Conn &conn)
    {
        std::string expected = viewerStamp();
        {
            std::lock_guard<std::mutex> guard(connMutex);
            if (expected.empty() || conn.build.empty()
                    || conn.build == expected
                    || conn.reloadPushed == expected)
                return;
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "{\"cmd\":\"reload\",\"cacheBust\":\"%s\"}",
                          expected.c_str());
            conn.pendingText.push_back(msg);
            conn.reloadPushed = expected;
        }
        std::printf("fcviewer server: viewer build stale, reload pushed\n");
    }

    void dispatchPick(DocGroup &g, const ScenePickRequest &req)
    {
        std::function<void(const ScenePickRequest &)> handler;
        {
            std::lock_guard<std::mutex> guard(handlerMutex);
            handler = g.pickHandler;
        }
        if (handler)
            handler(req);
    }

    /// Queue \a json for the connection identified by \a connId, if it
    /// is still with us. Any thread.
    void replyTo(uint64_t connId, const std::string &json)
    {
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns) {
            if (conn->id == connId) {
                conn->pendingText.push_back(json);
                return;
            }
        }
    }

    void dispatchControl(Conn &conn, std::string &&json)
    {
        std::function<void(SceneControlRequest &&)> handler;
        if (conn.group) {
            std::lock_guard<std::mutex> guard(handlerMutex);
            handler = conn.group->controlHandler;
        }
        SceneControlRequest req;
        req.json = std::move(json);
        {
            // The semantic layer refuses mutations for a view-only
            // connection; only it knows which ops write.
            std::lock_guard<std::mutex> guard(connMutex);
            req.viewOnly = conn.viewOnly;
        }
        const uint64_t connId = conn.id;
        req.reply = [this, connId](const std::string &answer) {
            replyTo(connId, answer);
        };
        if (handler) {
            handler(std::move(req));
            return;
        }
        // Answer rather than stay silent: correlate on the request id
        // when it has one, so the asker's timeout turns into a real
        // diagnosis.
        long long id = 0;
        char msg[96];
        if (jsonInt(req.json, "id", id))
            std::snprintf(msg, sizeof(msg),
                          "{\"id\":%lld,\"ok\":false,\"code\":"
                          "\"NoHandler\"}", id);
        else
            std::snprintf(msg, sizeof(msg),
                          "{\"op\":\"error\",\"code\":\"NoHandler\"}");
        req.reply(msg);
    }

#ifndef _WIN32
    bool start(int port)
    {
        listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0)
            return false;
        int on = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(uint16_t(port));
        if (::bind(listenFd, reinterpret_cast<sockaddr *>(&addr),
                   sizeof(addr)) < 0
                || ::listen(listenFd, 4) < 0) {
            ::close(listenFd);
            listenFd = -1;
            return false;
        }
        std::thread([this]() { acceptLoop(); }).detach();
        return true;
    }

    /// Close the listener and ask every connection to hang up. The
    /// accept loop wakes on the shutdown and exits; each connection's
    /// own loop notices the kick within a poll interval. A later
    /// start() binds anew.
    void stopListening()
    {
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (listenFd >= 0) {
                // shutdown() is what actually wakes a thread blocked
                // in accept(); close() alone may leave it parked.
                ::shutdown(listenFd, SHUT_RDWR);
                ::close(listenFd);
                listenFd = -1;
            }
            started = false;
        }
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns)
            conn->kicked = true;
    }

    void acceptLoop()
    {
        for (;;) {
            int fd = ::accept(listenFd, nullptr, nullptr);
            if (fd < 0)
                break;
            // One thread per connection: a WebSocket client keeps its
            // connection for the whole session and must not starve the
            // HTTP fallback (or a second viewer).
            std::thread([this, fd]() {
                handle(fd);
                ::close(fd);
            }).detach();
        }
    }

    static bool sendAll(int fd, const void *data, size_t size)
    {
        const char *p = static_cast<const char *>(data);
        while (size) {
            ssize_t n = ::send(fd, p, size, MSG_NOSIGNAL);
            if (n <= 0)
                return false;
            p += n;
            size -= size_t(n);
        }
        return true;
    }

    void handle(int fd)
    {
        // Read the request head (only the mesh batch carries a body).
        std::string req;
        char buf[1024];
        size_t headEnd = std::string::npos;
        while ((headEnd = req.find("\r\n\r\n")) == std::string::npos) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
                return;
            req.append(buf, size_t(n));
            if (req.size() > 16384)
                return;
        }
        bool post = req.compare(0, 5, "POST ") == 0;
        if (!post && req.compare(0, 4, "GET ") != 0)
            return;
        size_t verb = post ? 5 : 4;

        std::string reqBody;
        if (post) {
            size_t want = size_t(std::strtoul(
                headerValue(req, "content-length").c_str(), nullptr, 10));
            if (want > kMaxBatchBody)
                return;
            reqBody = req.substr(headEnd + 4);
            while (reqBody.size() < want) {
                ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                if (n <= 0)
                    return;
                reqBody.append(buf, size_t(n));
            }
            reqBody.resize(want);
        }

        std::string path = req.substr(verb, req.find(' ', verb) - verb);
        std::string query;
        auto q = path.find('?');
        if (q != std::string::npos) {
            query = path.substr(q + 1);
            path.resize(q);
        }

        // Who is asking, resolved before the door judges: the socket
        // peer, what a trusted proxy said the client's address is, and
        // the identity an authenticating front door asserted.
        char addr[80] = "";
        std::string peerIp;
        {
            sockaddr_in peer = {};
            socklen_t plen = sizeof(peer);
            if (::getpeername(fd, reinterpret_cast<sockaddr *>(&peer),
                              &plen) == 0) {
                char ip[64] = "";
                ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
                peerIp = ip;
                std::snprintf(addr, sizeof(addr), "%s:%u", ip,
                              unsigned(ntohs(peer.sin_port)));
            }
        }
        std::string fwd = forwardedFor(req, peerIp);
        std::string identity = assertedIdentity(req, peerIp);

        // The door (docs/MultiDocServe.md §8, docs/ShareAccess.md §2):
        // every route is gated, not just the hello — the polling
        // /scene, the blob and level fetches, the logs. With a grant
        // list set the grants judge token, identity, name and address
        // together; else the legacy shared token alone. A WebSocket
        // upgrade that fails still handshakes, because the token (and
        // the name a grant may require) may arrive in the hello
        // instead; until they do the connection is unauthorized and
        // gets nothing.
        std::string wsKey = headerValue(req, "sec-websocket-key");
        std::string presentedToken = queryValue(query, "token");
        Judgement entry = judge(presentedToken, identity,
                                queryValue(query, "client"),
                                fwd.empty() ? peerIp : fwd);
        bool authorized = entry.admitted;
        if (!authorized && wsKey.empty()) {
            static const char forbidden[] =
                "HTTP/1.1 403 Forbidden\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(fd, forbidden, sizeof(forbidden) - 1);
            return;
        }

        // /blob?key=<content key>: one out-of-band texture payload
        // (SceneDump.h, v26). Content addressed and immutable, so the
        // answer is cacheable forever — that is the whole point, a
        // republish must not re-send the pixels.
        if (path == "/blob") {
            std::vector<uint8_t> body;
            bool found = false;
            std::string key = queryValue(query, "key");
            if (isBlobKey(key)) {
                std::lock_guard<std::mutex> guard(mutex);
                auto it = blobs.find(key);
                if (it != blobs.end()) {
                    body = it->second;
                    found = true;
                }
            }
            if (!found) {
                static const char notFound[] =
                    "HTTP/1.1 404 Not Found\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
                sendAll(fd, notFound, sizeof(notFound) - 1);
                return;
            }
            char head[256];
            int n = std::snprintf(head, sizeof(head),
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Cache-Control: public, max-age=31536000, immutable\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n", body.size());
            if (sendAll(fd, head, size_t(n)))
                sendAll(fd, body.data(), body.size());
            return;
        }

        // POST /log, body = console lines: what a viewer saw, sent
        // back to the machine serving it.
        //
        // A phone has no console. Chrome on Android can only be
        // inspected over USB, which is no use to someone testing a
        // model over the network, and a page that has frozen or run
        // out of memory cannot be asked anything afterwards either. So
        // the viewer beacons its console here as it goes, and what
        // arrived before the freeze is on disk whether or not the page
        // survives.
        //
        // Deliberately dumb: append to the server's own log with a
        // prefix, no storage, no per-client state (invariant 7). The
        // reply is empty and immediate — a beacon is fire and forget,
        // and a viewer must never wait on its own logging.
        if (path == "/log") {
            for (size_t at = 0; at < reqBody.size();) {
                size_t nl = reqBody.find('\n', at);
                if (nl == std::string::npos)
                    nl = reqBody.size();
                if (nl > at)
                    std::fprintf(stderr, "viewer log: %.*s\n",
                                 int(nl - at), reqBody.data() + at);
                at = nl + 1;
            }
            // Straight to stderr, and flushed: this exists to survive
            // whatever the viewer is about to do, so it must be on
            // disk before the next line is read rather than sitting in
            // a buffer that a crash would take with it.
            std::fflush(stderr);
            static const char ok[] =
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Headers: *\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(fd, ok, sizeof(ok) - 1);
            return;
        }

        // POST /blobs, body = one content key per line: several
        // out-of-band payloads in a single response (SceneDump.h, v28).
        // Meshes are many and small, so a request per mesh would be
        // mostly round trips; textures stay on /blob, where one key
        // already fills a request. Framed reply:
        //   'FCBB', u32 count, then per key: 40 raw bytes, u32 length
        //   (0xffffffff = unknown key), payload.
        if (path == "/blobs") {
            std::vector<uint8_t> out;
            uint32_t count = 0;
            {
                std::lock_guard<std::mutex> guard(mutex);
                size_t pos = 0;
                while (pos < reqBody.size() && count < kMaxBatchKeys) {
                    size_t end = reqBody.find('\n', pos);
                    if (end == std::string::npos)
                        end = reqBody.size();
                    std::string key = reqBody.substr(pos, end - pos);
                    pos = end + 1;
                    while (!key.empty()
                           && (key.back() == '\r' || key.back() == ' '))
                        key.pop_back();
                    if (!isBlobKey(key))
                        continue;
                    ++count;
                    out.insert(out.end(), key.begin(), key.end());
                    auto it = blobs.find(key);
                    uint32_t len = it == blobs.end()
                        ? 0xffffffffu : uint32_t(it->second.size());
                    const uint8_t *lp = reinterpret_cast<uint8_t *>(&len);
                    out.insert(out.end(), lp, lp + 4);
                    if (it != blobs.end())
                        out.insert(out.end(), it->second.begin(),
                                   it->second.end());
                }
            }
            uint8_t header[8] = {'F', 'C', 'B', 'B'};
            std::memcpy(header + 4, &count, 4);
            out.insert(out.begin(), header, header + 8);
            char h[256];
            int n = std::snprintf(h, sizeof(h),
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Type: application/octet-stream\r\n"
                // Content addressed like /blob, but a batch is named by
                // the request body, which no HTTP cache keys on — so it
                // must not be stored. The viewer's own IndexedDB is
                // what makes a second visit free.
                "Cache-Control: no-store\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n", out.size());
            if (sendAll(fd, h, size_t(n)))
                sendAll(fd, out.data(), out.size());
            return;
        }

        // GET /level?source=<key>&level=<n>: ask for a declared level
        // of a mesh to be generated (docs/SceneStreaming.md §7, phase
        // 5c). A request for *work*, not bytes — the reply carries
        // nothing, because the answer arrives as an ordinary publish:
        // the worker finishes, the next root delta names the new key,
        // and from there it is a chunk like any other. 202 whether the
        // job is new, queued, or long done — idempotence is what lets
        // every viewer ask without coordination.
        if (path == "/level") {
            std::string source = queryValue(query, "source");
            uint32_t level = uint32_t(std::strtoul(
                queryValue(query, "level").c_str(), nullptr, 10));
            // A plain HTTP request names no connection, so the demand
            // is booked on the document the query names — absent, the
            // default (docs/MultiDocServe.md §4).
            DocGroup *g;
            {
                std::lock_guard<std::mutex> guard(mutex);
                g = joinable(queryValue(query, "doc"));
            }
            bool accepted = g && isBlobKey(source)
                && requestLevel(*g, source, level);
            static const char acceptedReply[] =
                "HTTP/1.1 202 Accepted\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            static const char refusedReply[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            if (accepted)
                sendAll(fd, acceptedReply, sizeof(acceptedReply) - 1);
            else
                sendAll(fd, refusedReply, sizeof(refusedReply) - 1);
            return;
        }

        // GET /decisions: pull every connected viewer's decision
        // journal (plan runs, fetches with their reasons, releases,
        // full-scene requests) — the backend command that reads what
        // the client decided and why, after the fact. Plain text, one
        // section per viewer.
        if (path == "/decisions") {
            std::vector<std::string> logs;
            requestDecisionLogs(4000, logs);
            std::string out;
            for (size_t i = 0; i < logs.size(); ++i) {
                out += "=== viewer " + std::to_string(i + 1) + " of "
                    + std::to_string(logs.size()) + " ===\n";
                out += logs[i];
                if (!out.empty() && out.back() != '\n')
                    out += '\n';
            }
            if (out.empty())
                out = "no viewers connected\n";
            char h[256];
            int n = std::snprintf(h, sizeof(h),
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n", out.size());
            if (sendAll(fd, h, size_t(n)))
                sendAll(fd, out.data(), out.size());
            return;
        }

        // /scene?v=<version>&s=<session>&doc=<name>: what the client
        // holds, and of which document (absent = the default). 204
        // while it is current. A version stated without a session, or
        // with one from another run of this backend, names a publish
        // that never happened here and is worth nothing (SceneDump.h,
        // v35) — the client is treated as holding nothing. The session
        // is per document, so the check happens against the group the
        // request lands on.
        uint64_t clientVersion = ~uint64_t(0);
        std::string v = queryValue(query, "v");
        if (!v.empty())
            clientVersion = std::strtoull(v.c_str(), nullptr, 10);
        std::string s = queryValue(query, "s");
        std::string doc = queryValue(query, "doc");
        if (path != "/scene" && path != "/scene.fcsd") {
            static const char notFound[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(fd, notFound, sizeof(notFound) - 1);
            return;
        }

        // WebSocket upgrade: handshake, then stay in the push loop.
        if (!wsKey.empty()) {
            if (handshake(fd, wsKey))
                wsLoop(fd, clientVersion, s, doc, entry, presentedToken,
                       addr, fwd, identity,
                       fwd.empty() ? peerIp : fwd);
            return;
        }

        std::vector<uint8_t> body;
        bool unknownDoc = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            DocGroup *g = joinable(doc);
            if (!g && !doc.empty())
                unknownDoc = true;
            std::vector<uint8_t> out;
            if (g) {
                if (!s.empty() && clientVersion != ~uint64_t(0)
                        && std::strtoull(s.c_str(), nullptr, 10)
                               != g->ensureSession())
                    clientVersion = 0;
                out = payloadFor(*g, clientVersion);
            }
            if (!out.empty()) {
                body.resize(8 + out.size());
                uint64_t served = g->version;
                std::memcpy(body.data(), &served, 8);
                std::memcpy(body.data() + 8, out.data(), out.size());
            }
        }
        if (unknownDoc) {
            static const char notFound[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(fd, notFound, sizeof(notFound) - 1);
            return;
        }
        if (body.empty()) {
            // Current, or nothing served at all — both are "you have
            // everything there is".
            static const char noContent[] =
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n";
            sendAll(fd, noContent, sizeof(noContent) - 1);
            return;
        }
        char head[256];
        int n = std::snprintf(head, sizeof(head),
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", body.size());
        if (sendAll(fd, head, size_t(n)))
            sendAll(fd, body.data(), body.size());
    }

    /// One `name=value` out of a request-target query string. The
    /// values in play are hex keys and decimal versions, so no
    /// percent-decoding is needed.
    static std::string queryValue(const std::string &query,
                                  const char *name)
    {
        std::string needle = std::string(name) + "=";
        size_t pos = 0;
        while (pos <= query.size()) {
            size_t end = query.find('&', pos);
            if (end == std::string::npos)
                end = query.size();
            if (query.compare(pos, needle.size(), needle) == 0)
                return query.substr(pos + needle.size(),
                                    end - pos - needle.size());
            pos = end + 1;
        }
        return {};
    }

    /// A well-formed content key: 40 lowercase hex characters. Checked
    /// before it reaches the store so a malformed request can never be
    /// anything but a 404.
    static bool isBlobKey(const std::string &key)
    {
        if (key.size() != 40)
            return false;
        for (char c : key) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
        }
        return true;
    }

    /// Case-insensitive lookup of one request-header value.
    static std::string headerValue(const std::string &req,
                                   const char *lowerName)
    {
        std::string lower(req);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string needle = std::string("\r\n") + lowerName + ":";
        auto pos = lower.find(needle);
        if (pos == std::string::npos)
            return {};
        pos += needle.size();
        auto end = req.find("\r\n", pos);
        if (end == std::string::npos)
            return {};
        std::string value = req.substr(pos, end - pos);
        auto b = value.find_first_not_of(" \t");
        auto e = value.find_last_not_of(" \t");
        if (b == std::string::npos)
            return {};
        return value.substr(b, e - b + 1);
    }

    static bool handshake(int fd, const std::string &key)
    {
        static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        Sha1 sha;
        sha.update(key.data(), key.size());
        sha.update(guid, sizeof(guid) - 1);
        uint8_t digest[20];
        sha.finish(digest);
        std::string accept = base64(digest, sizeof(digest));
        char head[256];
        int n = std::snprintf(head, sizeof(head),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n", accept.c_str());
        return sendAll(fd, head, size_t(n));
    }

    /// One server-to-client frame (unmasked): FIN + opcode, extended
    /// length as needed, then the payload.
    static bool sendFrame(int fd, uint8_t opcode,
                          const void *data, size_t size)
    {
        uint8_t head[10];
        size_t hn = 2;
        head[0] = uint8_t(0x80 | opcode);
        if (size < 126) {
            head[1] = uint8_t(size);
        }
        else if (size < 65536) {
            head[1] = 126;
            head[2] = uint8_t(size >> 8);
            head[3] = uint8_t(size);
            hn = 4;
        }
        else {
            head[1] = 127;
            for (int i = 0; i < 8; ++i)
                head[2 + i] = uint8_t(uint64_t(size) >> (56 - 8 * i));
            hn = 10;
        }
        return sendAll(fd, head, hn)
            && (!size || sendAll(fd, data, size));
    }

    /// Push loop of one WebSocket connection: send the versioned
    /// payload whenever it changes (including right after the
    /// handshake), and consume client frames (pick requests, pings,
    /// close).
    ///
    /// \a held is what the client said it already has, from the same
    /// `?v=&s=` on the upgrade request that the polling transport uses
    /// — read at handshake time, so a reconnecting viewer that is
    /// already current costs no payload and needs no round trip to say
    /// so. \a session is the raw `?s=`, checked against the joined
    /// group (sessions are per document); \a doc is the `?doc=` — a
    /// reconnect rejoins its document before the hello even arrives,
    /// which is what makes the held version mean anything. An unknown
    /// name joins nothing and leaves the rest to the hello.
    void wsLoop(int fd, uint64_t held, const std::string &session,
                const std::string &doc, const Judgement &entry,
                const std::string &presentedToken,
                const char *addr, const std::string &fwd,
                const std::string &identity,
                const std::string &matchAddr)
    {
        Conn conn;
        conn.fd = fd;
        conn.sent = held;
        conn.authorized = entry.admitted;
        conn.admitted = entry.admitted;
        conn.viewOnly = entry.viewOnly;
        conn.grant = entry.grant;
        conn.presentedToken = presentedToken;
        conn.addr = addr ? addr : "";
        conn.fwd = fwd;
        conn.identity = identity;
        conn.matchAddr = portlessAddress(matchAddr);
        conn.since = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> guard(mutex);
            conn.group = joinable(doc);
            // A version stated without a session, or with one from
            // another run (or another document's stream), names a
            // publish that never happened on this stream.
            if (!session.empty() && held != ~uint64_t(0)
                    && (!conn.group
                        || std::strtoull(session.c_str(), nullptr, 10)
                               != conn.group->ensureSession()))
                conn.sent = 0;
        }
        {
            std::lock_guard<std::mutex> guard(connMutex);
            conn.id = ++connIdCounter;
            if (conn.group)
                conn.docName = conn.group->name;
            conns.push_back(&conn);
        }
        notifyClientsChanged();
        wsLoopBody(fd, conn);
        {
            std::lock_guard<std::mutex> guard(connMutex);
            conns.erase(std::find(conns.begin(), conns.end(), &conn));
            // A collection waiting on this viewer would otherwise sit
            // out its full timeout.
            dumpCv.notify_all();
        }
        notifyClientsChanged();
    }

    void wsLoopBody(int fd, Conn &conn)
    {
        uint64_t &sent = conn.sent;
        std::string inbuf;
        int stampTick = 0;
        // Keepalive (docs/ShareAccess.md §5): a parked viewer sends and
        // receives nothing while the model is unchanged, and anything
        // with an idle timeout in the path drops it — Cloudflare closes
        // a quiet WebSocket at 100 s, 60 s is a common proxy default. A
        // ping whenever 30 s pass without a send keeps the stream
        // visibly alive in both directions (the browser answers the
        // pong itself), and turns a silently dead connection into a
        // send failure instead of a socket parked forever.
        auto lastSend = std::chrono::steady_clock::now();
        for (;;) {
            // Live bundle-stamp check (~every 5s at the 200ms poll):
            // a WASM rebuild while this backend serves pushes the
            // reload to already-connected pages, not just fresh hellos.
            if (++stampTick >= 25) {
                stampTick = 0;
                if (conn.viewer)
                    pushReloadIfStale(conn);
            }
            // Consume before pushing: a hello (or switch) that arrived
            // with the connection must pick the document *before* the
            // first payload goes out, or every named join would be
            // preceded by one spurious default-document snapshot. A
            // client that sends its hello on open is consumed within
            // the first poll; one that says nothing merely waits out
            // one interval for its first frame.
            pollfd p = {};
            p.fd = fd;
            p.events = POLLIN;
            int r = ::poll(&p, 1, 200);
            if (r < 0)
                return;
            if (r > 0) {
                char buf[65536];
                ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                if (n <= 0)
                    return;
                inbuf.append(buf, size_t(n));
                if (!consumeFrames(fd, conn, inbuf))
                    return;
            }
            // The host asked this connection closed (kickClient, a
            // server stop, or its own bad-token hello): drain what was
            // queued for it — a BadToken refusal rides there — then
            // say so, so a compliant viewer stops reconnecting, and
            // hang up.
            {
                bool kicked;
                std::vector<std::string> texts;
                {
                    std::lock_guard<std::mutex> guard(connMutex);
                    kicked = conn.kicked;
                    if (kicked)
                        texts.swap(conn.pendingText);
                }
                if (kicked) {
                    for (const std::string &text : texts)
                        sendFrame(fd, 1, text.data(), text.size());
                    static const char bye[] =
                        "{\"cmd\":\"error\",\"code\":\"Kicked\"}";
                    sendFrame(fd, 1, bye, sizeof(bye) - 1);
                    sendFrame(fd, 8, nullptr, 0);
                    return;
                }
            }
            std::vector<uint8_t> body;
            bool orphaned = false;
            const DocGroup *wasGroup = conn.group;
            {
                std::lock_guard<std::mutex> guard(mutex);
                // A torn-down document's connections re-home to the
                // default document, here on their own loop thread —
                // the only one allowed to touch conn.group
                // (docs/MultiDocServe.md §5). When it was the last,
                // the connection is joined to nothing and told so,
                // once: group stays null, so this does not refire.
                if (conn.group && conn.group->wasServed
                        && !conn.group->live) {
                    conn.group = defaultJoin();
                    sent = 0;
                    orphaned = !conn.group;
                }
                // What this connection is missing, which after a
                // coalesced tick may be several publishes rather than
                // one — the push loop sends the current scene, not
                // every version of it. An unauthorized connection
                // (token configured, none presented yet) gets no scene
                // bytes at all — its hello is what would open the door.
                if (conn.group && conn.authorized) {
                    DocGroup &g = *conn.group;
                    std::vector<uint8_t> out = payloadFor(g, sent);
                    if (!out.empty()) {
                        body.resize(8 + out.size());
                        uint64_t v = g.version;
                        std::memcpy(body.data(), &v, 8);
                        std::memcpy(body.data() + 8, out.data(),
                                    out.size());
                        sent = g.version;
                    }
                }
            }
            if (conn.group != wasGroup) {
                // Re-homed: keep the roster's document mirror true.
                {
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.docName = conn.group ? conn.group->name
                                              : std::string();
                }
                notifyClientsChanged();
            }
            if (orphaned) {
                std::lock_guard<std::mutex> guard(connMutex);
                conn.pendingText.push_back(
                    "{\"cmd\":\"error\",\"code\":\"NoDocument\"}");
            }
            if (!body.empty()
                    && !sendFrame(fd, 2, body.data(), body.size()))
                return;

            // Drain queued control messages (dumpFrame/reload) — sends
            // stay on this connection's thread.
            std::vector<std::string> texts;
            {
                std::lock_guard<std::mutex> guard(connMutex);
                texts.swap(conn.pendingText);
            }
            for (const std::string &text : texts) {
                if (!sendFrame(fd, 1, text.data(), text.size()))
                    return;
            }
            auto now = std::chrono::steady_clock::now();
            if (!body.empty() || !texts.empty()) {
                lastSend = now;
            } else if (now - lastSend >= std::chrono::seconds(30)) {
                if (!sendFrame(fd, 9, nullptr, 0))
                    return;
                lastSend = now;
            }
        }
    }

    /// Parse complete client frames off the front of \a inbuf; false
    /// ends the connection (close frame or protocol error).
    bool consumeFrames(int fd, Conn &conn, std::string &inbuf)
    {
        for (;;) {
            if (inbuf.size() < 2)
                return true;
            const uint8_t *p =
                reinterpret_cast<const uint8_t *>(inbuf.data());
            bool fin = (p[0] & 0x80) != 0;
            uint8_t opcode = p[0] & 0x0f;
            bool masked = (p[1] & 0x80) != 0;
            uint64_t len = p[1] & 0x7f;
            size_t off = 2;
            if (len == 126) {
                if (inbuf.size() < off + 2)
                    return true;
                len = (uint64_t(p[2]) << 8) | p[3];
                off += 2;
            }
            else if (len == 127) {
                if (inbuf.size() < off + 8)
                    return true;
                len = 0;
                for (int i = 0; i < 8; ++i)
                    len = (len << 8) | p[off + i];
                off += 8;
            }
            // Client frames must be masked. The cap accommodates a
            // dumpFrame pixel upload (a hi-DPI phone canvas is ~10MB
            // of RGBA).
            if (!masked || len > (64u << 20))
                return false;
            uint8_t mask[4];
            if (inbuf.size() < off + 4 + len)
                return true;
            std::memcpy(mask, p + off, 4);
            off += 4;
            std::vector<uint8_t> data(static_cast<size_t>(len), 0);
            for (size_t i = 0; i < len; ++i)
                data[i] = p[off + i] ^ mask[i & 3];
            inbuf.erase(0, off + size_t(len));

            switch (opcode) {
            case 8:     // close
                sendFrame(fd, 8, data.data(),
                          std::min<size_t>(data.size(), 2));
                return false;
            case 9:     // ping -> pong
                if (!sendFrame(fd, 10, data.data(), data.size()))
                    return false;
                break;
            case 0:     // continuation of a fragmented message
                if (!conn.fragOpcode)
                    break;      // stray continuation: ignore
                conn.fragData.append(
                        reinterpret_cast<const char *>(data.data()),
                        data.size());
                if (conn.fragData.size() > (64u << 20))
                    return false;
                if (fin) {
                    handleMessage(conn, conn.fragOpcode == 1,
                                  reinterpret_cast<const uint8_t *>(
                                      conn.fragData.data()),
                                  conn.fragData.size());
                    conn.fragData.clear();
                    conn.fragOpcode = 0;
                }
                break;
            case 1:
            case 2:
                if (!fin) {     // a browser may fragment large uploads
                    conn.fragOpcode = opcode;
                    conn.fragData.assign(
                            reinterpret_cast<const char *>(data.data()),
                            data.size());
                    break;
                }
                handleMessage(conn, opcode == 1, data.data(), data.size());
                break;
            default:    // pong: ignore
                break;
            }
        }
    }

    /// A complete client message: JSON control text (hello, and the
    /// dumpFrame answers' metadata rides binary), or one of the binary
    /// viewer events.
    void handleMessage(Conn &conn, bool text,
                       const uint8_t *bytes, size_t size)
    {
        if (text) {
            std::string json(reinterpret_cast<const char *>(bytes), size);
            // The door, at the moment the name arrives (docs/
            // MultiDocServe.md §8, docs/ShareAccess.md §2). Nothing
            // but a hello works while unauthorized; and with grants
            // set, *every* hello is judged — the self-declared name is
            // part of what a grant matches, and it only exists now. A
            // refusal is said out loud and the connection closed — no
            // scene bytes ever move.
            bool hello = json.find("\"cmd\":\"hello\"") != std::string::npos;
            if (hello) {
                std::string offered;
                if (!jsonStr(json, "token", offered) || offered.empty()) {
                    std::lock_guard<std::mutex> guard(connMutex);
                    offered = conn.presentedToken;
                }
                std::string name;
                jsonStr(json, "client", name);
                const bool grants = grantsActive();
                if (grants || !conn.authorized) {
                    Judgement entry = judge(offered, conn.identity, name,
                                            conn.matchAddr);
                    if (!entry.admitted) {
                        std::lock_guard<std::mutex> guard(connMutex);
                        conn.pendingText.push_back(grants
                            ? "{\"cmd\":\"error\",\"code\":\"Refused\"}"
                            : "{\"cmd\":\"error\",\"code\":\"BadToken\"}");
                        conn.kicked = true;
                        return;
                    }
                    conn.authorized = true;
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.admitted = true;
                    conn.presentedToken = offered;
                    conn.grant = entry.grant;
                    if (grants)
                        conn.viewOnly = entry.viewOnly;
                }
            }
            else if (!conn.authorized) {
                return;
            }
            // The semantic tier (docs/ThinClient.md §4.2) speaks "op";
            // the transport vocabulary below stays "cmd".
            if (json.find("\"op\":") != std::string::npos) {
                dispatchControl(conn, std::move(json));
                return;
            }
            // The viewer's model lost the delta chain and wants the
            // full current payload — over THIS socket, in order, which
            // is what makes the repair atomic: the push loop (same
            // thread) sends the full scene next iteration, and every
            // later delta bases on it. The HTTP route starves behind
            // the fetch window's own requests on a saturated link; the
            // socket is idle.
            if (json.find("\"cmd\":\"resync\"") != std::string::npos) {
                conn.sent = 0;
                return;
            }
            // The document listing, on demand — the same JSON the
            // serve/unserve push sends (docs/MultiDocServe.md §4).
            if (json.find("\"cmd\":\"docs\"") != std::string::npos) {
                std::string reply;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    reply = docsJsonLocked();
                }
                std::lock_guard<std::mutex> guard(connMutex);
                conn.pendingText.push_back(reply);
                return;
            }
            // Rename this connection (docs/MultiDocServe.md §4): the
            // hello's label, changed on an open connection, so a
            // viewer can name itself from its menu without
            // reconnecting. Nothing but the roster reads it — a rename
            // is free, the connection is the identity. But the door
            // must keep working for the *next* connection: when no
            // grant admits the new name, mint the live-only easing of
            // docs/ShareAccess.md §2 on this connection's token, so a
            // reload rejoins. It dies with the process; the panel
            // shows it apart, with a way to keep or drop it.
            if (json.find("\"cmd\":\"client\"") != std::string::npos) {
                std::string name;
                jsonStr(json, "name", name);
                if (conn.authorized && grantsActive()) {
                    std::string token;
                    uint64_t fromGrant;
                    {
                        std::lock_guard<std::mutex> guard(connMutex);
                        token = conn.presentedToken;
                        fromGrant = conn.grant;
                    }
                    Judgement entry = judge(token, conn.identity, name,
                                            conn.matchAddr);
                    if (!entry.admitted) {
                        addEasing(conn, token, name, fromGrant);
                    }
                    else if (entry.grant != fromGrant) {
                        std::lock_guard<std::mutex> guard(connMutex);
                        conn.grant = entry.grant;
                    }
                }
                {
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.client = name;
                }
                notifyClientsChanged();
                return;
            }
            // Leave the current document, join another. Versioning-
            // wise a fresh hello: the version resets even when the
            // name resolves to the document already joined, because
            // the viewer resets its model on switch and needs the full
            // snapshot back. A failed switch leaves the connection
            // where it was.
            if (json.find("\"cmd\":\"switch\"") != std::string::npos) {
                std::string name;
                jsonStr(json, "doc", name);
                if (joinDocument(conn, name)) {
                    conn.sent = 0;
                    notifyWork(*conn.group);
                }
                else {
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.pendingText.push_back(
                        "{\"cmd\":\"error\",\"code\":\"UnknownDocument\""
                        ",\"doc\":\"" + jsonEscape(name) + "\"}");
                }
                return;
            }
            if (hello) {
                {
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.viewer = true;
                    jsonStr(json, "build", conn.build);
                    jsonStr(json, "client", conn.client);
                }
                // The label just landed; the sharing roster shows it.
                notifyClientsChanged();
                // The document this viewer wants (docs/MultiDocServe.md
                // §4). Unknown name: an error and the listing, with the
                // connection alive but joined to nothing — the viewer
                // shows the list instead of dying.
                std::string docName;
                if (jsonStr(json, "doc", docName)
                        && !joinDocument(conn, docName)) {
                    conn.group = nullptr;
                    std::string docs;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        docs = docsJsonLocked();
                    }
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.pendingText.push_back(
                        "{\"cmd\":\"error\",\"code\":\"UnknownDocument\""
                        ",\"doc\":\"" + jsonEscape(docName) + "\"}");
                    conn.pendingText.push_back(docs);
                }
                // A serving backend schedules no frames of its own
                // (BGFXRenderer::animating / localAudience), and the
                // publish poll lives in the render path -- so ask for
                // the frame that publishes to this new viewer. It gets
                // the retained payload from the push loop either way;
                // this is what makes that payload current.
                if (conn.group)
                    notifyWork(*conn.group);
                // Bundle build stamp check: reload pages running a
                // superseded viewer build (any rebuild, not just
                // snapshot-format bumps).
                pushReloadIfStale(conn);
                // Version handshake (docs/RenderDebug.md §4.4): a viewer
                // built for an older snapshot format cannot parse what
                // this backend publishes — tell it to reload itself with
                // a cache-busting query parameter.
                long long snapshot = 0;
                if (jsonInt(json, "snapshot", snapshot)
                        && snapshot < (long long)sceneDumpVersion()) {
                    char msg[96];
                    std::snprintf(msg, sizeof(msg),
                                  "{\"cmd\":\"reload\",\"cacheBust\":\"v%u\"}",
                                  sceneDumpVersion());
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.pendingText.push_back(msg);
                }
                // Viewer policy push: the dropped-stream reconnect
                // budget (viewer default 10). FC_BGFX_VIEWER_RECONNECT
                // overrides it; -1 = infinite retries, a debugging aid
                // for long unattended sessions.
                static const long reconnect = [] {
                    const char *env =
                        std::getenv("FC_BGFX_VIEWER_RECONNECT");
                    return env ? std::strtol(env, nullptr, 10) : 10L;
                }();
                if (reconnect != 10) {
                    char msg[64];
                    std::snprintf(msg, sizeof(msg),
                                  "{\"cmd\":\"config\",\"reconnect\":%ld}",
                                  reconnect);
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.pendingText.push_back(msg);
                }
            }
            return;
        }
        if (!conn.authorized)
            return;
        if (size > 0 && bytes[0] == 'D') {
            handleFrameDump(bytes, size);
            return;
        }
        // A viewer's decision journal: 'L', u32 request id, u32 text
        // length, newline-joined lines.
        if (size >= 1 + 4 + 4 && bytes[0] == 'L') {
            uint32_t id, len;
            std::memcpy(&id, bytes + 1, 4);
            std::memcpy(&len, bytes + 5, 4);
            if (size < 9 + size_t(len))
                return;
            std::lock_guard<std::mutex> guard(connMutex);
            if (logCollect && logCollect->id == id) {
                std::string log(
                    reinterpret_cast<const char *>(bytes) + 9, len);
                // Attribution, when the hello offered a label
                // (docs/MultiDocServe.md §4).
                if (!conn.client.empty())
                    log = "client: " + conn.client + "\n" + log;
                logCollect->logs.push_back(std::move(log));
                dumpCv.notify_all();
            }
            return;
        }
        std::vector<uint8_t> data(bytes, bytes + size);
        handleEvent(conn, data);
    }

    /// A viewer's dumpFrame answer: 'D', u32 request id, u32 metadata
    /// length, the metadata JSON, u32 width, u32 height, then
    /// width*height RGBA8 pixels (bottom-up rows) — all little-endian.
    void handleFrameDump(const uint8_t *bytes, size_t size)
    {
        auto u32At = [bytes](size_t off) {
            uint32_t v;
            std::memcpy(&v, bytes + off, 4);
            return v;
        };
        if (size < 1 + 4 + 4)
            return;
        uint32_t id = u32At(1);
        uint32_t metaLen = u32At(5);
        size_t off = 9;
        if (size < off + metaLen + 8)
            return;
        ViewerFrameDump dump;
        dump.meta.assign(reinterpret_cast<const char *>(bytes) + off,
                         metaLen);
        off += metaLen;
        dump.width = int(u32At(off));
        dump.height = int(u32At(off + 4));
        off += 8;
        if (dump.width <= 0 || dump.height <= 0
                || size - off < size_t(dump.width) * dump.height * 4)
            return;
        dump.rgba.assign(bytes + off,
                         bytes + off + size_t(dump.width) * dump.height * 4);

        std::lock_guard<std::mutex> guard(connMutex);
        if (dumpCollect && dumpCollect->id == id) {
            dumpCollect->dumps.push_back(std::move(dump));
            dumpCv.notify_all();
        }
    }

    void handleEvent(Conn &conn, const std::vector<uint8_t> &data)
    {
        // A view-only connection's picks are dropped: selection is
        // shared room state, so changing it IS an edit
        // (docs/MultiDocServe.md §8).
        {
            std::lock_guard<std::mutex> guard(connMutex);
            if (conn.viewOnly)
                return;
        }
        // Pick request: 'P', flags byte, six little-endian floats
        // (world ray origin + direction).
        if (data.size() == 2 + 6 * sizeof(float) && data[0] == 'P') {
            ScenePickRequest req;
            req.modifiers = data[1];
            float v[6];
            std::memcpy(v, data.data() + 2, sizeof(v));
            for (int i = 0; i < 3; ++i) {
                req.origin[i] = v[i];
                req.dir[i] = v[3 + i];
            }
            if (conn.group)
                dispatchPick(*conn.group, req);
        }
        // Batched pick: 'B', count byte, then count * (modifiers byte + six
        // little-endian floats). The viewer batches a burst of client-side
        // selections into one message; each ray is dispatched in order so the
        // backend Gui::Selection ends up matching the client, and the queued
        // GUI-thread picks coalesce into a single scene republish.
        else if (data.size() >= 2 && data[0] == 'B') {
            const size_t stride = 1 + 6 * sizeof(float);
            const uint8_t n = data[1];
            if (data.size() == 2 + size_t(n) * stride) {
                size_t off = 2;
                for (uint8_t i = 0; i < n; ++i) {
                    ScenePickRequest req;
                    req.modifiers = data[off];
                    float v[6];
                    std::memcpy(v, data.data() + off + 1, sizeof(v));
                    for (int k = 0; k < 3; ++k) {
                        req.origin[k] = v[k];
                        req.dir[k] = v[3 + k];
                    }
                    if (conn.group)
                        dispatchPick(*conn.group, req);
                    off += stride;
                }
            }
        }
    }
#else
    bool start(int) { return false; }
    void stopListening() {}
#endif
};

/// Header names compare case-insensitively; store and match lowercase.
static std::string lowered(const char *s)
{
    std::string lower(s);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower;
}

SceneStreamServer &SceneStreamServer::instance()
{
    static SceneStreamServer server;
    return server;
}

void SceneStreamServer::setLevelThreadCap(int n)
{
    Private::s_levelThreadParam.store(n > 0 ? n : 0);
}

SceneStreamServer::Private *SceneStreamServer::ensure()
{
    if (!pimpl) {
        pimpl = new Private;
        // The env override presets the door secret (docs/MultiDocServe.md
        // §4) — the desktop Share dialog sets it through setToken.
        if (const char *env = std::getenv("FC_SERVE_TOKEN")) {
            if (*env)
                pimpl->tokenSecret = env;
        }
        if (const char *env = std::getenv("FC_SERVE_TRUST_PROXY"))
            pimpl->trustProxy.store(std::atoi(env) != 0);
        if (const char *env = std::getenv("FC_SERVE_IDENTITY_HEADER"))
            pimpl->identityHeaderName = lowered(env);
    }
    return pimpl;
}

bool SceneStreamServer::start(int port)
{
    Private *p = ensure();
    if (p->started)
        return running();
    p->started = true;
    return p->start(port);
}

bool SceneStreamServer::running() const
{
    return pimpl && pimpl->listenFd >= 0;
}

void SceneStreamServer::stop()
{
    if (pimpl)
        pimpl->stopListening();
}

void SceneStreamServer::setToken(const std::string &token)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->tokenMutex);
    p->tokenSecret = token;
}

void SceneStreamServer::setIdentityHeader(const std::string &name)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->tokenMutex);
    p->identityHeaderName = lowered(name.c_str());
}

std::string SceneStreamServer::identityHeader()
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->tokenMutex);
    return p->identityHeaderName;
}

void SceneStreamServer::setGrants(const std::vector<SceneGrant> &list)
{
    Private *p = ensure();
    {
        std::lock_guard<std::mutex> guard(p->tokenMutex);
        p->grantList = list;
        for (auto &g : p->grantList) {
            if (!g.id)
                g.id = ++p->grantIdCounter;
            else if (g.id > p->grantIdCounter)
                p->grantIdCounter = g.id;
        }
    }
    p->rejudgeConnections();
}

std::vector<SceneGrant> SceneStreamServer::grants()
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->tokenMutex);
    return p->grantList;
}

uint64_t SceneStreamServer::addGrant(SceneGrant grant)
{
    Private *p = ensure();
    uint64_t id;
    {
        std::lock_guard<std::mutex> guard(p->tokenMutex);
        grant.id = ++p->grantIdCounter;
        id = grant.id;
        p->grantList.push_back(std::move(grant));
    }
    p->rejudgeConnections();
    return id;
}

bool SceneStreamServer::removeGrant(uint64_t id)
{
    Private *p = ensure();
    bool removed = false;
    {
        std::lock_guard<std::mutex> guard(p->tokenMutex);
        auto it = std::find_if(p->grantList.begin(), p->grantList.end(),
                               [id](const SceneGrant &g) {
                                   return g.id == id;
                               });
        if (it != p->grantList.end()) {
            p->grantList.erase(it);
            removed = true;
        }
    }
    if (removed)
        p->rejudgeConnections();
    return removed;
}

std::string SceneStreamServer::token()
{
    return ensure()->tokenNow();
}

void SceneStreamServer::setTrustProxy(bool on)
{
    ensure()->trustProxy.store(on);
}

bool SceneStreamServer::trustProxy()
{
    return ensure()->trustProxy.load();
}

int SceneStreamServer::clients(std::vector<SceneClientInfo> &out)
{
    return ensure()->clients(out);
}

bool SceneStreamServer::setClientViewOnly(uint64_t id, bool viewOnly)
{
    return pimpl ? pimpl->setClientViewOnly(id, viewOnly) : false;
}

bool SceneStreamServer::kickClient(uint64_t id)
{
    return pimpl ? pimpl->kickClient(id) : false;
}

void SceneStreamServer::setClientsChangedNotifier(
        std::function<void()> notifier)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->handlerMutex);
    p->clientsChanged = std::move(notifier);
}

// Every entry point resolves its group from the optional document
// name (docs/MultiDocServe.md §3): empty names the default group,
// which is how the unnamed single-document callers keep working.

uint64_t SceneStreamServer::sessionId(const std::string &doc)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->mutex);
    return p->group(doc).ensureSession();
}

uint64_t SceneStreamServer::beginPublish(const void *publisher,
                                         const std::string &doc)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->mutex);
    Private::DocGroup &g = p->group(doc);
    g.ensureSession();
    if (!g.publisher)
        g.publisher = publisher;
    else if (g.publisher != publisher)
        return 0;
    // Handed out before the payload exists, so a serializer that fails
    // leaves a gap. Versions are monotonic, not gapless: a consumer
    // compares them, it does not count them.
    return ++g.handedOut;
}

void SceneStreamServer::endPublish(const void *publisher,
                                   const std::string &doc)
{
    if (!pimpl)
        return;
    std::lock_guard<std::mutex> guard(pimpl->mutex);
    Private::DocGroup &g = pimpl->group(doc);
    if (g.publisher == publisher)
        g.publisher = nullptr;
}

void SceneStreamServer::publish(ScenePublish &&pub, const std::string &doc)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->mutex);
    Private::DocGroup &g = p->group(doc);
    if (pub.version <= g.version)
        return;   // superseded before it was installed
    // A gap would make the history lie about what a viewer is missing:
    // merging across it would skip whatever the missing publish said.
    if (!g.history.empty()
            && g.history.back().version + 1 != pub.version)
        g.history.clear();
    g.history.push_back({pub.version, std::move(pub.changed),
                         std::move(pub.removed)});
    while (g.history.size() > Private::kHistory)
        g.history.pop_front();

    g.payload = std::move(pub.payload);
    g.spans = pub.spans;
    g.objects = pub.objects;
    // Version and payload move together, and only here: what is served
    // must always be the bytes the version names.
    g.version = pub.version;
    p->retireBlobs(g);
}

void SceneStreamServer::publishBlob(const std::string &key,
                                    std::vector<uint8_t> &&data,
                                    const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    p->addBlob(*g, key, std::move(data));
}

bool SceneStreamServer::retainBlob(const std::string &key, uint32_t *size,
                                   const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    return p->retainBlob(*g, key, size);
}

bool SceneStreamServer::requestLevel(const std::string &source,
                                     uint32_t level, const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    return p->requestLevel(*g, source, level);
}

std::string SceneStreamServer::builtLevel(const std::string &source,
                                          uint32_t level, uint32_t *size,
                                          const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    return p->builtLevel(*g, source, level, size);
}

size_t SceneStreamServer::levelsBuilt(const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    return p->levelsBuilt(*g);
}


void SceneStreamServer::setPickHandler(
        std::function<void(const ScenePickRequest &)> handler,
        const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    std::lock_guard<std::mutex> guard(p->handlerMutex);
    g->pickHandler = std::move(handler);
}

void SceneStreamServer::setControlHandler(
        std::function<void(SceneControlRequest &&)> handler,
        const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    std::lock_guard<std::mutex> guard(p->handlerMutex);
    g->controlHandler = std::move(handler);
}

void SceneStreamServer::setWorkNotifier(std::function<void()> notifier,
                                        const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    std::lock_guard<std::mutex> guard(p->handlerMutex);
    g->workNotifier = std::move(notifier);
}

void SceneStreamServer::setDocumentInfo(const std::string &doc,
                                        const std::string &label)
{
    Private *p = ensure();
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        Private::DocGroup &g = p->group(doc);
        g.label = label;
        if (!g.live && !g.name.empty()) {
            g.live = true;
            g.wasServed = true;
            g.serveSeq = ++p->serveSeqCounter;
        }
    }
    // Every connected viewer's document menu redraws from this push.
    p->pushDocs();
}

void SceneStreamServer::releaseGroup(const std::string &doc)
{
    if (!pimpl)
        return;
    Private::DocGroup *g = nullptr;
    {
        std::lock_guard<std::mutex> guard(pimpl->mutex);
        auto it = pimpl->groups.find(doc);
        if (it == pimpl->groups.end())
            return;
        g = &it->second;
        g->publisher = nullptr;
        // Off the wire: no longer joinable, gone from the listing,
        // and each of its connections re-homes on its own loop's next
        // tick (wsLoopBody).
        g->live = false;
        // Queued jobs would book work and wake a notifier that is
        // about to be cleared; drop them. A job a worker already holds
        // finishes against the retained group node, harmlessly.
        auto &q = pimpl->levelQueue;
        q.erase(std::remove_if(q.begin(), q.end(),
                               [g](const Private::LevelJob &job) {
                                   return job.group == g;
                               }),
                q.end());
    }
    {
        std::lock_guard<std::mutex> guard(pimpl->handlerMutex);
        g->pickHandler = nullptr;
        g->controlHandler = nullptr;
        g->workNotifier = nullptr;
    }
    // The document left the listing; tell the menus.
    pimpl->pushDocs();
}

void SceneStreamServer::broadcastControl(const std::string &json)
{
    ensure()->broadcastControl(json);
}

int SceneStreamServer::requestFrameDumps(int mode, int timeoutMs,
                                         std::vector<ViewerFrameDump> &dumps)
{
    return ensure()->requestFrameDumps(mode, timeoutMs, dumps);
}

int SceneStreamServer::requestDecisionLogs(int timeoutMs,
                                           std::vector<std::string> &logs)
{
    return ensure()->requestDecisionLogs(timeoutMs, logs);
}
