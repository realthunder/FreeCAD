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

// Boost.Asio before anything else: on Windows it has to see winsock2.h
// before a Qt or FreeCAD header pulls windows.h in (docs/SceneServerPort.md
// sec 8 item 1 -- no Windows box here to verify on).
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

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
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

using namespace Render;

class SceneStreamServer::Private {
public:
    // Defined up front: these are used from nested-class member bodies
    // (struct Conn) and from members declared above their old position,
    // where the enclosing class is still incomplete. All pure string
    // helpers and plain structs -- the socket code stays under the
    // POSIX guard below.
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

    static bool isLoopback(const std::string &ip)
    {
        return ip == "127.0.0.1" || ip == "::1";
    }

    /// One HTTP request as the routing sees it, whatever read it off
    /// the wire (docs/SceneServerPort.md sec 7.1): the POSIX head
    /// reader below today, a Beast parser after stage 2.
    struct HttpRequest {
        std::string method;   ///< "GET" or "POST"; nothing else gets here
        std::string path;     ///< the request target up to its '?'
        std::string query;    ///< what followed the '?', if anything
        /// Names lowercased (they match case-insensitively), values
        /// trimmed; a repeated name keeps its first value.
        std::map<std::string, std::string> headers;
        std::string body;     ///< the POST body, complete
        std::string peerIp;   ///< the socket peer; empty when unknown
        unsigned peerPort = 0;

        std::string header(const char *lowerName) const
        {
            auto it = headers.find(lowerName);
            return it == headers.end() ? std::string() : it->second;
        }

        /// The peer as the roster shows it, ip:port -- the port is
        /// what tells two tunnelled viewers apart.
        std::string peer() const
        {
            if (peerIp.empty())
                return {};
            return peerIp + ":" + std::to_string(peerPort);
        }
    };

    /// A request target into path and query. The values in play are
    /// hex keys and decimal versions, so nothing is percent-decoded.
    static void splitTarget(const std::string &target, HttpRequest &req)
    {
        auto q = target.find('?');
        req.path = target.substr(0, q);
        req.query = q == std::string::npos ? std::string()
                                           : target.substr(q + 1);
    }

    /// What the routing answers with. Every reply carries
    /// `Access-Control-Allow-Origin: *` and closes the connection; the
    /// transport adds those, the reason phrase and the length.
    struct HttpReply {
        int status = 404;
        const char *contentType = nullptr;    ///< sent when set
        const char *cacheControl = nullptr;   ///< sent when set
        /// `Access-Control-Allow-Headers: *` -- the /log beacon's
        /// preflight wants it.
        bool allowAnyHeader = false;
        std::vector<uint8_t> body;

        void set(int s, const char *type = nullptr,
                 const char *cache = nullptr)
        {
            status = s;
            contentType = type;
            cacheControl = cache;
        }
    };

    std::mutex mutex;
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

    /// The client address to believe for the connection \a req came
    /// on. Empty unless trust is on, the socket peer is loopback, and
    /// a header named one.
    ///
    /// The peer check is the whole security of this: a proxy or the
    /// local end of a tunnel is on this machine, so anything arriving
    /// from elsewhere is talking to us directly and its headers are
    /// its own invention.
    std::string forwardedFor(const HttpRequest &req)
    {
        if (!trustProxy.load())
            return {};
        if (!isLoopback(req.peerIp))
            return {};
        // Cloudflare guarantees CF-Connecting-IP (docs/ShareAccess.md
        // §5); X-Forwarded-For is everyone else's spelling, a list of
        // client, proxy, proxy… whose first entry is the one that
        // reached the outermost proxy.
        std::string value = req.header("cf-connecting-ip");
        if (value.empty())
            value = req.header("x-forwarded-for");
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
    std::string assertedIdentity(const HttpRequest &req)
    {
        if (!trustProxy.load())
            return {};
        if (!isLoopback(req.peerIp))
            return {};
        std::string configured;
        {
            std::lock_guard<std::mutex> guard(tokenMutex);
            configured = identityHeaderName;
        }
        std::string value;
        if (!configured.empty()) {
            value = req.header(configured.c_str());
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
                value = req.header(name);
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

    /// Secret comparison that takes the same time whichever byte
    /// first differs: `==` returns on the first mismatch, so how long
    /// a refusal takes states how much of the token was right, and a
    /// token can be guessed a byte at a time. The length is not part
    /// of the secret (these are fixed-width hex), so an early out on
    /// it is fine; the bytes are folded before anything is decided.
    static bool secretEqual(const std::string &a, const std::string &b)
    {
        if (a.size() != b.size())
            return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < a.size(); ++i)
            diff |= static_cast<unsigned char>(a[i] ^ b[i]);
        return diff == 0;
    }

    /// Whether a request carrying \a query may pass the door: open
    /// server, or a matching `?token=`.
    bool tokenOk(const std::string &query)
    {
        std::string secret = tokenNow();
        return secret.empty()
            || secretEqual(queryValue(query, "token"), secret);
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
            if (!g.token.empty() && !secretEqual(g.token, token))
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
            out.admitted = tokenSecret.empty()
                || secretEqual(token, tokenSecret);
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
        std::function<void(uint64_t)> clientClosedHandler;

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
        sweepBlobs();
    }

    /// Drop from the store what no group names at all, and with each
    /// dropped chunk the level memos that pointed at it. Called with
    /// \a mutex held — from retireBlobs and from a group release.
    void sweepBlobs()
    {
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

    /// The transport's end of one connection -- what the protocol core
    /// may ask of the socket without knowing it (docs/SceneServerPort.md
    /// sec 7.1): a POSIX fd today, a Beast stream on its strand after
    /// stage 2. Owned by the transport for exactly the connection's
    /// life, and only ever reached through a Conn on the roster, under
    /// connMutex.
    struct Link {
        virtual ~Link() = default;
        /// Act on the connection's flags, its outbox and the scene now
        /// rather than at its next tick (docs/SceneServerPort.md sec
        /// 7.3): something was queued, a version was published, a kick,
        /// a server stop. Posted, never run inline; any thread, and
        /// coalesced by Conn::nudge so a burst costs one post.
        virtual void wake() = 0;
    };

    /// One queued outbound message (Conn::outbox).
    struct Outgoing {
        enum Kind : uint8_t {
            Text,    ///< a control JSON: a text frame
            Scene,   ///< the versioned scene bytes: a binary frame
            Frame,   ///< a streamed frame (sendBinary): a binary frame
            Close    ///< the end: a close frame, then hang up
        };
        Kind kind = Text;
        std::vector<uint8_t> data;
    };

    /// One live WebSocket connection, registered by its transport
    /// (openConnection). All sends stay on that connection's strand:
    /// what is to go out is queued on the outbox and drained by its
    /// writer, woken through the link.
    struct Conn {
        Link *link = nullptr;
        /// Stable identity for cross-thread reply routing (the Conn
        /// itself is owned by its Session): a control reply looks the
        /// connection up by id under connMutex and is dropped when it
        /// is gone. Never reused within a run.
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
        /// The outbox (docs/SceneServerPort.md sec 6.4): what is to go
        /// out, in order, drained by this connection's own loop.
        /// Guarded by connMutex; appended through the helpers below.
        std::deque<Outgoing> outbox;
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
        /// connMutex): its farewell is on the outbox, and its own loop
        /// sends that and hangs up. Set only through kick().
        bool kicked = false;
        /// The stamp a reload was already pushed for — one push per
        /// bundle generation, no loops.
        std::string reloadPushed;
        /// A wake is posted and has not run yet (guarded by connMutex):
        /// the next nudge is free. Cleared by the transport on the
        /// strand before it acts, so anything queued after that clear
        /// posts anew.
        bool wakePosted = false;

        /// Have the link act now (sec 7.3): every append and every
        /// publish ends here, so nothing queued from the host waits for
        /// the tick. connMutex held.
        void nudge()
        {
            if (!link || wakePosted)
                return;
            wakePosted = true;
            link->wake();
        }

        /// Queue a control JSON. connMutex held.
        void queueText(const std::string &json)
        {
            Outgoing item;
            item.kind = Outgoing::Text;
            item.data.assign(json.begin(), json.end());
            outbox.push_back(std::move(item));
            nudge();
        }

        /// Queue the versioned scene bytes. connMutex held.
        void queueScene(std::vector<uint8_t> &&body)
        {
            Outgoing item;
            item.kind = Outgoing::Scene;
            item.data = std::move(body);
            outbox.push_back(std::move(item));
            nudge();
        }

        /// Queue a streamed frame. One still queued is replaced, in
        /// the place it held: a frame is a state, not an event, and a
        /// slow link should see the newest one. connMutex held.
        void queueFrame(std::vector<uint8_t> &&data)
        {
            for (Outgoing &item : outbox) {
                if (item.kind == Outgoing::Frame) {
                    item.data = std::move(data);
                    nudge();
                    return;
                }
            }
            Outgoing item;
            item.kind = Outgoing::Frame;
            item.data = std::move(data);
            outbox.push_back(std::move(item));
            nudge();
        }

        /// Ask this connection closed: after whatever is already
        /// queued -- a BadToken refusal rides there -- it is told
        /// {"cmd":"error","code":"Kicked"}, so a compliant viewer
        /// stops reconnecting, then closed by its own loop. Once;
        /// connMutex held. Not a ban -- changing the token is.
        void kick()
        {
            if (kicked)
                return;
            kicked = true;
            queueText("{\"cmd\":\"error\",\"code\":\"Kicked\"}");
            Outgoing bye;
            bye.kind = Outgoing::Close;
            outbox.push_back(std::move(bye));
            nudge();
        }
    };
    std::mutex connMutex;
    std::vector<Conn *> conns;
    uint64_t connIdCounter = 0;   ///< guarded by connMutex
    /// Pre-auth accept caps (accepted): every accepted socket -- HTTP
    /// and WS alike -- counts until its Session is destroyed.
    std::mutex acceptCountMutex;
    int activeConns = 0;                       ///< guarded above
    std::map<std::string, int> activeByIp;     ///< non-loopback only
    static constexpr int kMaxConns = 128;
    static constexpr int kMaxConnsPerIp = 16;
    std::condition_variable dumpCv;    ///< guarded by connMutex
    /// In-flight dumpFrame collection (one at a time).
    struct DumpCollect {
        uint32_t id = 0;
        size_t expected = 0;
        /// The connections the request actually went to, each removed
        /// by its one answer. Without it the request id is the whole
        /// key: one viewer could answer N times and fill the
        /// collection with its own frames, displacing the answers the
        /// other viewers are still rendering.
        std::set<uint64_t> awaited;
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
        std::set<uint64_t> awaited;   ///< as DumpCollect::awaited
        std::vector<std::string> logs;
    };
    LogCollect *logCollect = nullptr;

    void broadcastControl(const std::string &json)
    {
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns) {
            if (conn->viewer)
                conn->queueText(json);
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
                    conn->queueText(
                        "{\"cmd\":\"error\",\"code\":\"Refused\"}");
                    conn->kick();
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
                    conn->queueText(
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
                conn->kick();
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
            if (conn->viewer) {
                conn->queueText(msg);
                collect.awaited.insert(conn->id);
            }
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
            if (conn->viewer) {
                conn->queueText(msg);
                collect.awaited.insert(conn->id);
            }
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

    /// GET fallback for the viewer bundle itself: map \a pathIn onto
    /// the FC_BGFX_VIEWER_BUILD directory and answer with the file, so one
    /// hostname (one tunnel) carries the page, the scene stream and
    /// the blobs alike (docs/ShareAccess.md §5). Returns false when
    /// the feature is off, the path is not a plausible bundle file, or
    /// the file does not exist — the caller then 404s as before.
    ///
    /// Everything is served `no-store`: the bundle files are not
    /// content-hashed, and the stamp reload's cache-bust parameter
    /// only renames the page URL, not the script and wasm behind it —
    /// a cached copy of those could survive a rebuild. When assets
    /// grow content-hashed names they can take the /blob policy
    /// (immutable) instead.
    ///
    /// The file read is the second piece of blocking work of
    /// docs/SceneServerPort.md sec 6.5, after payloadFor: on stage 2's
    /// shared io thread it is posted to the worker pool.
    bool serveViewerFile(const std::string &pathIn, HttpReply &reply)
    {
        static const char *dir = std::getenv("FC_BGFX_VIEWER_BUILD");
        if (!dir || !*dir)
            return false;
        std::string path = pathIn == "/" ? std::string("/fcviewer.html")
                                         : pathIn;
        if (path.empty() || path[0] != '/'
                || path.find("..") != std::string::npos)
            return false;
        for (char c : path)
            if (!std::isalnum(static_cast<unsigned char>(c))
                    && !std::strchr("/._-", c))
                return false;
        auto dot = path.rfind('.');
        if (dot == std::string::npos)
            return false;
        std::string ext = path.substr(dot + 1);
        const char *type = nullptr;
        if (ext == "html")
            type = "text/html; charset=utf-8";
        else if (ext == "js" || ext == "mjs")
            type = "text/javascript";
        else if (ext == "wasm")
            type = "application/wasm";
        else if (ext == "css")
            type = "text/css";
        else if (ext == "svg")
            type = "image/svg+xml";
        else if (ext == "png")
            type = "image/png";
        else if (ext == "ico")
            type = "image/x-icon";
        else if (ext == "json" || ext == "map")
            type = "application/json";
        else if (ext == "data" || ext == "bin")
            type = "application/octet-stream";
        if (!type)
            return false;
        std::FILE *f = std::fopen((std::string(dir) + path).c_str(), "rb");
        if (!f)
            return false;
        std::vector<uint8_t> body;
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            body.insert(body.end(), buf, buf + n);
        std::fclose(f);
        reply.set(200, type, "no-store");
        reply.body = std::move(body);
        return true;
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
            conn.queueText(msg);
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
                conn->queueText(json);
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
        req.client = connId;
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

    /// Pre-auth flood control (docs/SceneServerPort.md sec 4): each
    /// connection may legitimately buffer tens of MiB (frame + fragment
    /// caps), and the door only judges after the WS handshake -- with
    /// no cap a LAN or direct-mode peer could hold unbounded threads
    /// and memory. Every accepted socket, HTTP and WS alike, is
    /// admitted here and released once its handler is done. Loopback
    /// is exempt from the per-address cap: the tunnel front door
    /// (cloudflared) funnels every remote client through it, and the
    /// global cap still bounds it. Keyed on the socket peer for now;
    /// stage 4 of the port keys it on the judged address, which is
    /// what a gateway on another host needs.
    bool admitPeer(const std::string &ip, bool loopback)
    {
        std::lock_guard<std::mutex> guard(acceptCountMutex);
        int perIp = 0;
        auto it = activeByIp.find(ip);
        if (it != activeByIp.end())
            perIp = it->second;
        if (activeConns >= kMaxConns
                || (!loopback && perIp >= kMaxConnsPerIp))
            return false;
        ++activeConns;
        if (!loopback)
            activeByIp[ip] = perIp + 1;
        return true;
    }

    void releasePeer(const std::string &ip, bool loopback)
    {
        std::lock_guard<std::mutex> guard(acceptCountMutex);
        --activeConns;
        if (!loopback) {
            auto it = activeByIp.find(ip);
            if (it != activeByIp.end() && --it->second <= 0)
                activeByIp.erase(it);
        }
    }

    /// Where a request goes once routed: answered with the reply, or
    /// upgraded to a WebSocket connection built from the bootstrap.
    enum class Route { Reply, Upgrade };

    /// Everything a WebSocket connection inherits from the request
    /// that opened it, resolved by route() and handed to the transport
    /// to build the Conn from (openConnection).
    struct WsBootstrap {
        /// What the client said it already has, from the same `?v=&s=`
        /// on the upgrade request that the polling transport uses --
        /// read at handshake time, so a reconnecting viewer that is
        /// already current costs no payload and needs no round trip to
        /// say so. ~0 = nothing stated.
        uint64_t held = ~uint64_t(0);
        std::string session;        ///< the raw `?s=`, judged per group
        /// The `?doc=`: a reconnect rejoins its document before the
        /// hello even arrives, which is what makes the held version
        /// mean anything. An unknown name joins nothing and leaves the
        /// rest to the hello.
        std::string doc;
        Judgement entry;            ///< the door's verdict on the upgrade
        std::string presentedToken; ///< the `?token=`, if any
        std::string addr;           ///< peer ip:port, for the roster
        std::string fwd;            ///< forwardedFor, if any
        std::string identity;       ///< assertedIdentity, if any
        std::string matchAddr;      ///< what the door judges by
    };

    /// Route one request (docs/SceneServerPort.md sec 7.1, item 1):
    /// the whole of what the server says over HTTP, and the decision
    /// to upgrade, with no socket in sight. The transport reads the
    /// request, calls this, and writes the reply or runs the
    /// connection. Two routes block on this thread -- the /scene
    /// serialization and the /decisions collection -- which is the
    /// section 6.5 work that leaves the io thread in stage 2.
    Route route(const HttpRequest &req, HttpReply &reply, WsBootstrap &boot)
    {
        const std::string &path = req.path;
        const std::string &query = req.query;
        const std::string &reqBody = req.body;

        // Who is asking, resolved before the door judges: the socket
        // peer, what a trusted proxy said the client's address is, and
        // the identity an authenticating front door asserted.
        const std::string &peerIp = req.peerIp;
        std::string fwd = forwardedFor(req);
        std::string identity = assertedIdentity(req);

        // The door (docs/MultiDocServe.md §8, docs/ShareAccess.md §2):
        // every route is gated, not just the hello — the polling
        // /scene, the blob and level fetches, the logs. With a grant
        // list set the grants judge token, identity, name and address
        // together; else the legacy shared token alone. A WebSocket
        // upgrade that fails still handshakes, because the token (and
        // the name a grant may require) may arrive in the hello
        // instead; until they do the connection is unauthorized and
        // gets nothing.
        std::string wsKey = req.header("sec-websocket-key");

        // The viewer bundle answers before the door: it is the
        // published viewer code, not scene bytes, and the browser
        // fetches the page's subresources without the link's ?token=
        // tail — behind the door the page would load and its script
        // would 403. The door still judges everything that carries
        // scene data.
        if (wsKey.empty() && serveViewerFile(path, reply))
            return Route::Reply;

        std::string presentedToken = queryValue(query, "token");
        Judgement entry = judge(presentedToken, identity,
                                queryValue(query, "client"),
                                fwd.empty() ? peerIp : fwd);
        bool authorized = entry.admitted;
        if (!authorized && wsKey.empty()) {
            reply.status = 403;
            return Route::Reply;
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
                reply.status = 404;
                return Route::Reply;
            }
            reply.set(200, "application/octet-stream",
                      "public, max-age=31536000, immutable");
            reply.body = std::move(body);
            return Route::Reply;
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
            reply.status = 204;
            reply.allowAnyHeader = true;
            return Route::Reply;
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
            // Content addressed like /blob, but a batch is named by
            // the request body, which no HTTP cache keys on -- so it
            // must not be stored. The viewer's own IndexedDB is
            // what makes a second visit free.
            reply.set(200, "application/octet-stream", "no-store");
            reply.body = std::move(out);
            return Route::Reply;
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
            reply.status = accepted ? 202 : 404;
            return Route::Reply;
        }

        // GET /decisions: pull every connected viewer's decision
        // journal (plan runs, fetches with their reasons, releases,
        // full-scene requests) — the backend command that reads what
        // the client decided and why, after the fact. Plain text, one
        // section per viewer. Blocks up to the collection timeout.
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
            reply.set(200, "text/plain; charset=utf-8");
            reply.body.assign(out.begin(), out.end());
            return Route::Reply;
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
            reply.status = 404;
            return Route::Reply;
        }

        // WebSocket upgrade: the transport handshakes and runs the
        // connection from what the request resolved to.
        if (!wsKey.empty()) {
            boot.held = clientVersion;
            boot.session = s;
            boot.doc = doc;
            boot.entry = entry;
            boot.presentedToken = presentedToken;
            boot.addr = req.peer();
            boot.fwd = fwd;
            boot.identity = identity;
            boot.matchAddr = fwd.empty() ? peerIp : fwd;
            return Route::Upgrade;
        }

        // The polling transport's payload: serialized here, under the
        // scene mutex, on the caller's thread (section 6.5).
        std::vector<uint8_t> body;
        bool unknownDoc = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            DocGroup *g = joinable(doc);
            if (!g && !doc.empty())
                unknownDoc = true;
            std::vector<uint8_t> out;
            if (g) {
                // A version without a session, or with another run's,
                // names a publish that never happened here (the v35
                // contract above) — treat as holding nothing.
                if (clientVersion != ~uint64_t(0)
                        && (s.empty()
                            || std::strtoull(s.c_str(), nullptr, 10)
                                   != g->ensureSession()))
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
            reply.status = 404;
            return Route::Reply;
        }
        if (body.empty()) {
            // Current, or nothing served at all — both are "you have
            // everything there is".
            reply.status = 204;
            return Route::Reply;
        }
        reply.set(200, "application/octet-stream");
        reply.body = std::move(body);
        return Route::Reply;
    }

    /// Register a connection built from the request that opened it
    /// (route's WsBootstrap): join the document it named, seed its
    /// version, give it an id, put it on the roster. Called on the
    /// connection's own loop thread, before its first tick; the
    /// transport has already set conn.link.
    void openConnection(Conn &conn, const WsBootstrap &boot)
    {
        conn.sent = boot.held;
        conn.authorized = boot.entry.admitted;
        conn.admitted = boot.entry.admitted;
        conn.viewOnly = boot.entry.viewOnly;
        conn.grant = boot.entry.grant;
        conn.presentedToken = boot.presentedToken;
        conn.addr = boot.addr;
        conn.fwd = boot.fwd;
        conn.identity = boot.identity;
        conn.matchAddr = portlessAddress(boot.matchAddr);
        conn.since = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> guard(mutex);
            conn.group = joinable(boot.doc);
            // A version stated without a session, or with one from
            // another run (or another document's stream), names a
            // publish that never happened on this stream.
            if (boot.held != ~uint64_t(0)
                    && (boot.session.empty() || !conn.group
                        || std::strtoull(boot.session.c_str(), nullptr, 10)
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
    }

    /// The connection is over: off the roster, the closed handler
    /// told, the roster cue fired. Called on the loop thread as it
    /// leaves; after this nothing can reach conn or its link.
    void closeConnection(Conn &conn)
    {
        {
            std::lock_guard<std::mutex> guard(connMutex);
            conns.erase(std::find(conns.begin(), conns.end(), &conn));
            // A collection waiting on this viewer would otherwise sit
            // out its full timeout.
            dumpCv.notify_all();
        }
        // The group's source may hold state for this connection (a
        // served viewport): told after the id has left the roster, so
        // nothing it queues in answer can land.
        {
            std::function<void(uint64_t)> closed;
            if (conn.group) {
                std::lock_guard<std::mutex> guard(handlerMutex);
                closed = conn.group->clientClosedHandler;
            }
            if (closed)
                closed(conn.id);
        }
        notifyClientsChanged();
    }

    /// One tick's scene push, in three steps (docs/SceneServerPort.md
    /// sec 6.5). What must run on the connection's own strand -- the
    /// re-home after a teardown, and reading which group and held
    /// version to serialize from -- is beginScenePush; the
    /// serialization itself, under the scene mutex for as long as it
    /// takes, is computeScenePush on a worker thread, so no other
    /// connection waits behind it; commitScenePush, back on the strand,
    /// queues the bytes unless the connection moved on meanwhile. The
    /// group pointer is safe to hold across the worker: groups are
    /// never erased, only marked not live.
    struct ScenePush {
        DocGroup *group = nullptr;
        uint64_t held = 0;
        uint64_t version = 0;
        std::vector<uint8_t> body;
    };

    /// Strand. True when there is something to serialize.
    bool beginScenePush(Conn &conn, ScenePush &push)
    {
        push = ScenePush();
        bool orphaned = false;
        const DocGroup *wasGroup = conn.group;
        {
            std::lock_guard<std::mutex> guard(mutex);
            // A torn-down document's connections re-home to the
            // default document, here on their own strand -- the only
            // place allowed to touch conn.group (docs/MultiDocServe.md
            // sec 5). When it was the last, the connection is joined
            // to nothing and told so, once: group stays null, so this
            // does not refire.
            if (conn.group && conn.group->wasServed
                    && !conn.group->live) {
                conn.group = defaultJoin();
                conn.sent = 0;
                orphaned = !conn.group;
            }
            // An unauthorized connection (token configured, none
            // presented yet) gets no scene bytes at all -- its hello is
            // what would open the door.
            if (conn.group && conn.authorized) {
                push.group = conn.group;
                push.held = conn.sent;
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
            conn.queueText("{\"cmd\":\"error\",\"code\":\"NoDocument\"}");
        }
        return push.group != nullptr;
    }

    /// Worker. What the connection is missing, which after a coalesced
    /// tick may be several publishes rather than one -- the push sends
    /// the current scene, not every version of it.
    void computeScenePush(ScenePush &push)
    {
        std::lock_guard<std::mutex> guard(mutex);
        DocGroup &g = *push.group;
        std::vector<uint8_t> out = payloadFor(g, push.held);
        if (out.empty())
            return;
        push.version = g.version;
        push.body.resize(8 + out.size());
        std::memcpy(push.body.data(), &push.version, 8);
        std::memcpy(push.body.data() + 8, out.data(), out.size());
    }

    /// Strand. Dropped when a hello or a switch changed the group, or
    /// reset what the connection holds, while the bytes were being
    /// made: the next tick computes the right ones.
    void commitScenePush(Conn &conn, ScenePush &push)
    {
        if (push.body.empty() || conn.group != push.group
                || conn.sent != push.held)
            return;
        conn.sent = push.version;
        std::lock_guard<std::mutex> guard(connMutex);
        conn.queueScene(std::move(push.body));
    }

    /// A version was published: every connection looks at the scene
    /// now (sec 7.3). All of them rather than the group's, because
    /// which group a connection is in is its own strand's business;
    /// the look itself (pushDue) is cheap and answers no for the rest.
    /// Called with no lock held: nudge takes connMutex, and the
    /// publisher has just released the scene mutex.
    void wakeAll()
    {
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns)
            conn->nudge();
    }

    /// Strand. Whether a scene push would send anything: the
    /// connection is joined and in, and holds a version other than the
    /// group's -- or its group was torn down and it must re-home
    /// (beginScenePush does that). The cheap look that keeps a wake
    /// for a control reply from costing a worker hop and the scene
    /// mutex for nothing.
    bool pushDue(Conn &conn)
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!conn.group)
            return false;
        if (conn.group->wasServed && !conn.group->live)
            return true;
        return conn.authorized && conn.group->version != conn.sent;
    }

    /// Ask every connection to hang up: each one's own writer sends
    /// the farewell and closes (Conn::kick). The connection half of a
    /// stop; closeListener() is the other.
    void closeAll()
    {
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns)
            conn->kick();
    }

    // ------------------------------------------------------------------
    // The transport (docs/SceneServerPort.md sec 6 and 7.2): Boost.Beast,
    // async, one strand per connection. Everything above this line is
    // the protocol core and knows no socket. This is the listener, the
    // HTTP exchange, and the WebSocket connection scheduled as three
    // stackless coroutines on its strand (sec 5.4): a reader, a writer
    // that parks on an empty outbox and is woken through Link::wake(),
    // and a tick that is the fallback for the scene push -- the push
    // itself is a fourth, short-lived coroutine started by a wake (sec
    // 7.3). The blocking work -- route() for HTTP, payloadFor() for a
    // push -- runs on a worker pool with only its completion back on
    // the strand (sec 6.5).
    // ------------------------------------------------------------------

    using tcp = boost::asio::ip::tcp;

    static constexpr int kWorkerThreads = 4;
    static constexpr int kTickMs = 200;
    /// A request head or a handshake that takes longer than this is
    /// dropped: the audit's missing read deadline (sec 4).
    static constexpr int kRequestSeconds = 30;
    /// Keepalive (docs/ShareAccess.md sec 5): a parked viewer sends and
    /// receives nothing while the model is unchanged, and anything with
    /// an idle timeout in the path drops it -- Cloudflare at 100 s, 60 s
    /// a common proxy default. Beast pings at half this when nothing has
    /// arrived, the browser answers the pong itself, and a peer silent
    /// for the whole of it is closed rather than parked forever -- which
    /// also bounds a write stalled against a peer that stopped reading,
    /// the job of the POSIX loop's 20 s send timeout.
    static constexpr int kIdleSeconds = 60;
    /// The cap accommodates a dumpFrame pixel upload (a hi-DPI phone
    /// canvas is ~10MB of RGBA).
    static constexpr std::size_t kMaxMessage = std::size_t(64) << 20;

    /// The io thread and the worker pool, made on the first start() and
    /// kept for the life of the server (Private is never destroyed): a
    /// stop() closes the listener and the connections, not these, so a
    /// restart binds anew on the same machinery.
    struct Io {
        boost::asio::io_context ctx{1};
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard;
        /// The section 6.5 pool. A few threads rather than one: a
        /// /decisions route waits out its timeout here and must not
        /// hold a scene push behind it.
        boost::asio::thread_pool workers{kWorkerThreads};
        Io()
            : guard(boost::asio::make_work_guard(ctx))
        {
            std::thread([this]() { ctx.run(); }).detach();
        }
    };
    Io *io = nullptr;                              ///< guarded by mutex
    std::shared_ptr<tcp::acceptor> acceptor;       ///< guarded by mutex

    static boost::beast::websocket::stream_base::timeout wsTimeouts()
    {
        boost::beast::websocket::stream_base::timeout t;
        t.handshake_timeout = std::chrono::seconds(kRequestSeconds);
        t.idle_timeout = std::chrono::seconds(kIdleSeconds);
        t.keep_alive_pings = true;
        return t;
    }

    struct Session;

    /// One accepted socket for its whole life: the HTTP exchange, and
    /// when the request upgrades, the WebSocket connection. Owned by
    /// the handlers in flight (shared_ptr), so it lives exactly as long
    /// as something can still complete on it; the peer's accept-cap
    /// slot is given back when it is destroyed.
    struct Session : Link, std::enable_shared_from_this<Session> {
        Private &srv;
        /// The stream: Beast's tcp_stream (a socket with a deadline
        /// timer) that the WebSocket layer wraps once the request
        /// upgrades. Its executor is this connection's strand, so every
        /// completion on it, and everything posted to that executor,
        /// runs serialized -- the invariant the POSIX loop kept with its
        /// one thread (sec 6.2).
        boost::beast::websocket::stream<boost::beast::tcp_stream> ws;
        std::string ip;
        unsigned port = 0;
        bool loopback = false;
        boost::beast::flat_buffer inbuf;
        boost::beast::http::request_parser<boost::beast::http::string_body> parser;
        boost::beast::http::response<boost::beast::http::vector_body<uint8_t>> res;
        HttpRequest req;
        HttpReply reply;
        WsBootstrap boot;
        Route routed = Route::Reply;
        /// The connection, once the request upgraded: on the roster
        /// between openConnection and closeConnection (\a open).
        Conn conn;
        bool open = false;
        boost::asio::steady_timer tick;
        int stampTick = 0;
        /// The writer: what it is sending, and where it parked -- the
        /// coroutine's state, an int, kept here so any thread can wake
        /// it by posting a handler built from it (sec 6.6).
        Outgoing outItem;
        boost::asio::coroutine writerCo;
        bool writerParked = false;
        /// Coroutines still running (reader, writer, tick, and a push
        /// while one is in flight); the connection is torn down when
        /// the last of them ends.
        int live = 0;
        bool closing = false;
        /// The push in flight, computed off the strand (sec 6.5). One
        /// at a time (\a pushing); a wake meanwhile asks for another
        /// after it (\a pushAgain), which then sends the current scene
        /// -- several publishes coalesce into one push, as they did
        /// under a tick.
        ScenePush push;
        bool pushing = false;
        bool pushAgain = false;

        Session(Private &s, tcp::socket &&socket)
            : srv(s)
            , ws(std::move(socket))
            , tick(ws.get_executor())
        {
        }

        ~Session() override
        {
            srv.releasePeer(ip, loopback);
        }

        /// Link: act on the outbox and the scene now. From any thread.
        void wake() override
        {
            boost::asio::post(ws.get_executor(),
                              [self = shared_from_this()]() { self->onWake(); });
        }

        /// Strand. The posted half of wake(): clear the coalescing flag
        /// first, so a nudge that lands from here on posts again, then
        /// drain the outbox and look at the scene.
        void onWake()
        {
            {
                std::lock_guard<std::mutex> guard(srv.connMutex);
                conn.wakePosted = false;
            }
            wakeWriter();
            startPush();
        }

        /// Strand. Start a scene push if one would send anything and
        /// none is in flight; note the ask otherwise.
        void startPush()
        {
            srv.Session_startPush(*this);
        }

        /// Strand. The push in flight ended.
        void endPush()
        {
            pushing = false;
            ended();
            if (pushAgain) {
                pushAgain = false;
                startPush();
            }
        }

        /// Strand. Resume the parked writer; nothing if it is running.
        void wakeWriter()
        {
            srv.Session_wakeWriter(*this);
        }

        /// Strand. Park the writer at the state it carries.
        void park(const boost::asio::coroutine &state)
        {
            writerCo = state;
            writerParked = true;
        }

        bool kickedNow()
        {
            std::lock_guard<std::mutex> guard(srv.connMutex);
            return conn.kicked;
        }

        /// Strand. The next outbox item into \a outItem.
        bool takeNext()
        {
            std::lock_guard<std::mutex> guard(srv.connMutex);
            if (conn.outbox.empty())
                return false;
            outItem = std::move(conn.outbox.front());
            conn.outbox.pop_front();
            return true;
        }

        /// The parsed request into the routing's struct. False for a
        /// verb the server does not speak.
        bool toRequest()
        {
            auto &r = parser.get();
            req.method = std::string(r.method_string());
            if (req.method != "GET" && req.method != "POST")
                return false;
            splitTarget(std::string(r.target()), req);
            for (const auto &f : r) {
                std::string name(f.name_string());
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                // emplace: a repeated name keeps its first value
                req.headers.emplace(name, std::string(f.value()));
            }
            req.body = std::move(r.body());
            req.peerIp = ip;
            req.peerPort = port;
            return true;
        }

        /// The routed reply into the response: the CORS header every
        /// answer carries, the optional type and cache policy, the
        /// length (none on a 204), Connection: close, then the body.
        void toResponse()
        {
            namespace http = boost::beast::http;
            res.version(11);
            res.result(unsigned(reply.status));
            res.set(http::field::access_control_allow_origin, "*");
            if (reply.allowAnyHeader)
                res.set(http::field::access_control_allow_headers, "*");
            if (reply.contentType)
                res.set(http::field::content_type, reply.contentType);
            if (reply.cacheControl)
                res.set(http::field::cache_control, reply.cacheControl);
            res.keep_alive(false);
            res.body() = std::move(reply.body);
            if (reply.status != 204)
                res.content_length(res.body().size());
        }

        /// The HTTP exchange is over, answered or not: hang up.
        void finishHttp()
        {
            boost::beast::error_code ec;
            ws.next_layer().socket().shutdown(tcp::socket::shutdown_send, ec);
            ws.next_layer().close();
        }

        /// The request upgraded and the handshake is done: the
        /// connection joins the roster and its three coroutines start.
        void startConnection()
        {
            srv.Session_startConnection(*this);
        }

        void ended()
        {
            if (--live == 0 && open) {
                open = false;
                srv.closeConnection(conn);
            }
        }

        /// The reader is done (the peer closed, a protocol error, the
        /// idle timeout): nothing more will be sent either.
        void endReader()
        {
            closing = true;
            tick.cancel();
            if (writerParked) {
                writerParked = false;
                --live;
            }
            boost::beast::get_lowest_layer(ws).close();
            ended();
        }

        /// The writer is done (the Close item went out, or a write
        /// failed): the socket closes under the reader.
        void endWriter()
        {
            closing = true;
            tick.cancel();
            boost::beast::get_lowest_layer(ws).close();
            ended();
        }

        void endTick()
        {
            ended();
        }
    };

#include <boost/asio/yield.hpp>

    /// The HTTP exchange of a Session: read the request, route it off
    /// the io thread, then either write the reply and hang up or accept
    /// the WebSocket and start the connection.
    struct Http : boost::asio::coroutine {
        std::shared_ptr<Session> s;
        explicit Http(std::shared_ptr<Session> p) : s(std::move(p)) {}
        void operator()(boost::beast::error_code ec = {}, std::size_t = 0)
        {
            Session &c = *s;
            reenter(this) {
                // A head over 16 KiB or a body over the batch cap drops
                // the connection unanswered, as it always did.
                c.parser.header_limit(16384);
                c.parser.body_limit(kMaxBatchBody);
                c.ws.next_layer().expires_after(
                        std::chrono::seconds(kRequestSeconds));
                yield boost::beast::http::async_read(
                        c.ws.next_layer(), c.inbuf, c.parser, *this);
                if (ec || !c.toRequest()) {
                    c.finishHttp();
                    return;
                }
                // route() off the io thread (sec 6.5): /scene serializes
                // under the scene mutex, /decisions waits out a timeout.
                yield boost::asio::post(c.srv.io->workers,
                                        [self = *this]() mutable {
                    Session &cc = *self.s;
                    cc.routed = cc.srv.route(cc.req, cc.reply, cc.boot);
                    boost::asio::post(cc.ws.get_executor(), std::move(self));
                });
                if (c.routed == Route::Upgrade) {
                    // The WebSocket layer keeps its own timeouts; the
                    // tcp_stream's would fire under it.
                    c.ws.next_layer().expires_never();
                    c.ws.set_option(wsTimeouts());
                    yield c.ws.async_accept(c.parser.get(), *this);
                    if (ec) {
                        c.finishHttp();
                        return;
                    }
                    c.inbuf.consume(c.inbuf.size());
                    c.startConnection();
                    return;
                }
                c.toResponse();
                yield boost::beast::http::async_write(
                        c.ws.next_layer(), c.res, *this);
                c.finishHttp();
            }
        }
    };

    /// The reader of a connection: one complete message at a time --
    /// Beast reassembles fragments, answers pings and closes, and holds
    /// control frames to their rules -- handed to the protocol.
    struct Reader : boost::asio::coroutine {
        std::shared_ptr<Session> s;
        explicit Reader(std::shared_ptr<Session> p) : s(std::move(p)) {}
        void operator()(boost::beast::error_code ec = {}, std::size_t = 0)
        {
            Session &c = *s;
            reenter(this) {
                for (;;) {
                    yield c.ws.async_read(c.inbuf, *this);
                    if (ec)
                        break;
                    // A kicked connection reads nothing more (the POSIX
                    // loop shut its read side): what still arrives is
                    // dropped, and only the farewell goes out.
                    if (!c.kickedNow())
                        c.srv.handleMessage(
                                c.conn, c.ws.got_text(),
                                static_cast<const uint8_t *>(c.inbuf.cdata().data()),
                                c.inbuf.size());
                    c.inbuf.consume(c.inbuf.size());
                    // Whatever the message queued goes out now, not at
                    // the next tick -- and a hello, a switch or a
                    // resync gets its scene now too.
                    c.wakeWriter();
                    c.startPush();
                }
                c.endReader();
            }
        }
    };

    /// The writer of a connection: drains the outbox in order with one
    /// write in flight, stops at a Close item, and parks -- returns,
    /// its state saved on the Session -- when the outbox is empty
    /// (sec 6.6). wakeWriter() resumes it from that state.
    struct Writer : boost::asio::coroutine {
        std::shared_ptr<Session> s;
        explicit Writer(std::shared_ptr<Session> p) : s(std::move(p)) {}
        void operator()(boost::beast::error_code ec = {}, std::size_t = 0)
        {
            Session &c = *s;
            reenter(this) {
                for (;;) {
                    while (!c.takeNext()) {
                        yield c.park(*this);
                    }
                    if (c.outItem.kind == Outgoing::Close) {
                        yield c.ws.async_close(
                                boost::beast::websocket::close_code::normal,
                                *this);
                        break;
                    }
                    c.ws.text(c.outItem.kind == Outgoing::Text);
                    yield c.ws.async_write(
                            boost::asio::buffer(c.outItem.data), *this);
                    if (ec)
                        break;
                }
                c.endWriter();
            }
        }
    };

    /// One scene push of a connection (sec 7.3): the three steps of
    /// sec 6.5 -- what to serialize, on the strand; the serialization
    /// on the worker pool; the bytes onto the outbox, back on the
    /// strand -- then the writer is woken. Started by a wake, whether
    /// from a publish, from the message that changed what this
    /// connection holds, or from the tick.
    struct Push : boost::asio::coroutine {
        std::shared_ptr<Session> s;
        explicit Push(std::shared_ptr<Session> p) : s(std::move(p)) {}
        void operator()(boost::beast::error_code = {}, std::size_t = 0)
        {
            Session &c = *s;
            reenter(this) {
                if (c.srv.beginScenePush(c.conn, c.push)) {
                    yield boost::asio::post(c.srv.io->workers,
                                            [self = *this]() mutable {
                        Session &cc = *self.s;
                        cc.srv.computeScenePush(cc.push);
                        boost::asio::post(cc.ws.get_executor(),
                                          std::move(self));
                    });
                    if (!c.closing)
                        c.srv.commitScenePush(c.conn, c.push);
                }
                c.wakeWriter();
                c.endPush();
            }
        }
    };

    /// The tick of a connection: every kTickMs, the live bundle-stamp
    /// check (~every 5 s) and a look at the scene -- the fallback
    /// behind the wakes, and the path that re-homes a connection whose
    /// document was torn down. Ends with the connection, or when it is
    /// kicked: a kicked connection pushes nothing more.
    struct Tick : boost::asio::coroutine {
        std::shared_ptr<Session> s;
        explicit Tick(std::shared_ptr<Session> p) : s(std::move(p)) {}
        void operator()(boost::beast::error_code ec = {}, std::size_t = 0)
        {
            Session &c = *s;
            reenter(this) {
                for (;;) {
                    if (c.closing)
                        break;
                    c.tick.expires_after(std::chrono::milliseconds(kTickMs));
                    yield c.tick.async_wait(*this);
                    if (ec || c.closing || c.kickedNow())
                        break;
                    // A WASM rebuild while this backend serves pushes
                    // the reload to already-connected pages, not just
                    // fresh hellos.
                    if (++c.stampTick >= 25) {
                        c.stampTick = 0;
                        if (c.conn.viewer)
                            c.srv.pushReloadIfStale(c.conn);
                    }
                    c.wakeWriter();
                    c.startPush();
                }
                c.endTick();
            }
        }
    };

#include <boost/asio/unyield.hpp>

    void Session_wakeWriter(Session &c)
    {
        if (!c.writerParked)
            return;
        c.writerParked = false;
        Writer w{c.shared_from_this()};
        static_cast<boost::asio::coroutine &>(w) = c.writerCo;
        w();
    }

    void Session_startPush(Session &c)
    {
        if (c.closing || !c.open || c.kickedNow())
            return;
        if (c.pushing) {
            c.pushAgain = true;
            return;
        }
        if (!pushDue(c.conn))
            return;
        c.pushing = true;
        ++c.live;
        Push{c.shared_from_this()}();
    }

    void Session_startConnection(Session &c)
    {
        c.ws.read_message_max(kMaxMessage);
        // One frame per message, as the POSIX sender wrote them; the
        // default would split every push into 4 KiB frames.
        c.ws.auto_fragment(false);
        c.conn.link = &c;
        c.srv.openConnection(c.conn, c.boot);
        c.open = true;
        c.live = 3;
        c.writerParked = true;
        Reader{c.shared_from_this()}();
        Tick{c.shared_from_this()}();
        // A fresh writer resumes from its top and parks again at once
        // unless something is already queued. No push yet: the scene
        // follows the hello (the reader starts it), which is what says
        // which snapshot format the viewer reads; a viewer that never
        // says hello gets it from the tick, as before.
        c.wakeWriter();
    }

    /// The listener: accept on the io thread, each socket onto a strand
    /// of its own, through the pre-auth caps, into a Session.
    void doAccept(const std::shared_ptr<tcp::acceptor> &acc)
    {
        acc->async_accept(boost::asio::make_strand(io->ctx),
                          [this, acc](boost::system::error_code ec,
                                      tcp::socket socket) {
            if (ec == boost::asio::error::operation_aborted || !acc->is_open())
                return;
            if (!ec)
                accepted(std::move(socket));
            doAccept(acc);
        });
    }

    void accepted(tcp::socket &&socket)
    {
        boost::system::error_code ec;
        auto ep = socket.remote_endpoint(ec);
        std::string ip = ec ? std::string() : ep.address().to_string();
        const unsigned port = ec ? 0 : ep.port();
        const bool loopback = isLoopback(ip);
        if (!admitPeer(ip, loopback))
            return;     // the socket closes with the object
        // A control reply is a small frame that may follow a large
        // one; with Nagle on it would sit behind the peer's delayed
        // ACK, tens of milliseconds on a path this design measures in
        // single ones (sec 7.3).
        socket.set_option(tcp::no_delay(true), ec);
        auto s = std::make_shared<Session>(*this, std::move(socket));
        s->ip = ip;
        s->port = port;
        s->loopback = loopback;
        boost::asio::dispatch(s->ws.get_executor(),
                              [s]() { Http{s}(); });
    }

    bool start(int port)
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!io)
            io = new Io;
        auto acc = std::make_shared<tcp::acceptor>(io->ctx);
        boost::system::error_code ec;
        acc->open(tcp::v4(), ec);
        if (ec)
            return false;
        acc->set_option(boost::asio::socket_base::reuse_address(true), ec);
        acc->bind(tcp::endpoint(tcp::v4(), uint16_t(port)), ec);
        if (!ec)
            acc->listen(4, ec);
        if (ec)
            return false;
        acceptor = acc;
        doAccept(acc);
        return true;
    }

    bool listening()
    {
        std::lock_guard<std::mutex> guard(mutex);
        return acceptor && acceptor->is_open();
    }

    /// Close the listener: no new connection is accepted, and a later
    /// start() binds anew. Closed on the io thread, where its accept
    /// runs, and waited for, so that a start() right after finds the
    /// port free. The connection half of a stop is closeAll().
    void closeListener()
    {
        std::shared_ptr<tcp::acceptor> acc;
        {
            std::lock_guard<std::mutex> guard(mutex);
            acc = std::move(acceptor);
            started = false;
        }
        if (!acc)
            return;
        std::promise<void> done;
        boost::asio::post(io->ctx, [acc, &done]() {
            boost::system::error_code ec;
            acc->close(ec);
            done.set_value();
        });
        done.get_future().wait();
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
                        conn.queueText(grants
                            ? "{\"cmd\":\"error\",\"code\":\"Refused\"}"
                            : "{\"cmd\":\"error\",\"code\":\"BadToken\"}");
                        conn.kick();
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
                conn.queueText(reply);
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
                    conn.queueText(
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
                    conn.queueText(
                        "{\"cmd\":\"error\",\"code\":\"UnknownDocument\""
                        ",\"doc\":\"" + jsonEscape(docName) + "\"}");
                    conn.queueText(docs);
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
                    conn.queueText(msg);
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
                    conn.queueText(msg);
                }
            }
            return;
        }
        if (!conn.authorized)
            return;
        if (size > 0 && bytes[0] == 'D') {
            handleFrameDump(conn, bytes, size);
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
            if (logCollect && logCollect->id == id
                    && logCollect->awaited.erase(conn.id)) {
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
    void handleFrameDump(Conn &conn, const uint8_t *bytes,
                         size_t size)
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
        if (dumpCollect && dumpCollect->id == id
                && dumpCollect->awaited.erase(conn.id)) {
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
    return pimpl && pimpl->listening();
}

void SceneStreamServer::stop()
{
    if (!pimpl)
        return;
    pimpl->closeListener();
    pimpl->closeAll();
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
    std::unique_lock<std::mutex> guard(p->mutex);
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
    guard.unlock();
    // The version is installed: every connection looks at it now
    // (docs/SceneServerPort.md sec 7.3), not at its next tick.
    p->wakeAll();
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

void SceneStreamServer::setClientClosedHandler(
        std::function<void(uint64_t)> handler, const std::string &doc)
{
    Private *p = ensure();
    Private::DocGroup *g;
    {
        std::lock_guard<std::mutex> guard(p->mutex);
        g = &p->group(doc);
    }
    std::lock_guard<std::mutex> guard(p->handlerMutex);
    g->clientClosedHandler = std::move(handler);
}

bool SceneStreamServer::sendControl(uint64_t client, const std::string &json)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->connMutex);
    for (Private::Conn *conn : p->conns) {
        if (conn->id == client) {
            conn->queueText(json);
            return true;
        }
    }
    return false;
}

bool SceneStreamServer::sendBinary(uint64_t client, std::vector<uint8_t> &&data)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->connMutex);
    for (Private::Conn *conn : p->conns) {
        if (conn->id == client) {
            conn->queueFrame(std::move(data));
            return true;
        }
    }
    return false;
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
        // A closed document's chunk references must not pin the store
        // for the process lifetime: clear its generations — a re-serve
        // republishes — then drop what nothing names any more.
        g->pendingKeys.clear();
        g->currentKeys.clear();
        g->previousKeys.clear();
        // The level memos go with them: requestLevel early-returns on
        // a recorded ask, so an entry outliving its purged job would
        // answer 202 forever without ever building on a re-serve.
        g->levelAsked.clear();
        g->levelBuilt.clear();
        pimpl->sweepBlobs();
    }
    {
        std::lock_guard<std::mutex> guard(pimpl->handlerMutex);
        g->pickHandler = nullptr;
        g->controlHandler = nullptr;
        g->workNotifier = nullptr;
        g->clientClosedHandler = nullptr;
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
