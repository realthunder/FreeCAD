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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
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
    std::vector<uint8_t> payload;
    /// The version \a payload was built for; both change together, in
    /// publish(). \a handedOut runs ahead of it — a version is claimed
    /// before the payload that carries it can be serialized.
    uint64_t version = 0;
    uint64_t handedOut = 0;
    int listenFd = -1;
    bool started = false;

    /// Where \a payload's object list sits, so it can be narrowed for a
    /// viewer that is behind (SceneDump.h, spliceObjectDelta).
    SceneSnapshot::RootSpans spans;

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
    std::deque<Change> history;
    static const size_t kHistory = 32;

    /// This run of the backend, and who is numbering its versions
    /// (SceneServer.h, beginPublish). Both guarded by \a mutex.
    uint64_t session = 0;
    const void *publisher = nullptr;

    /// The payload for a viewer holding \a held: the difference since
    /// it, when the history reaches back that far, and the whole root
    /// otherwise. Empty when there is nothing to send. Call with
    /// \a mutex held.
    ///
    /// The merge is last-wins per objectKey, which is what makes this
    /// safe to serve: the last thing said about an object is its
    /// current state, so the result names only keys the publish in
    /// flight still names — never a chunk that has since been retired.
    std::vector<uint8_t> payloadFor(uint64_t held)
    {
        if (payload.empty() || held == version)
            return {};
        // No manifest layout (nothing to narrow), or a viewer holding
        // nothing, or one whose version predates what we remember.
        if (!spans.listEnd || !held || held > version || history.empty()
                || history.front().version > held + 1)
            return payload;

        std::map<uint64_t, const SceneSnapshot::ObjectEntry *> changed;
        std::set<uint64_t> removed;
        for (const auto &h : history) {
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
        if (!spliceObjectDelta(payload, spans, held, entries, gone, out))
            return payload;
        return out;
    }

    uint64_t ensureSession()
    {
        if (!session) {
            // Wall clock, so that two runs of this process — which is
            // exactly the case the id exists for — cannot collide the
            // way a counter or an address could.
            session = uint64_t(std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                    .count());
            if (!session)
                session = 1;   // 0 means "unknown" on the wire
        }
        return session;
    }

    /// Out-of-band texture payloads, addressed by content key and
    /// served over GET /blob?key= — the snapshot only names them
    /// (SceneDump.h, v26). Immutable by construction, so a viewer may
    /// cache one forever.
    std::map<std::string, std::vector<uint8_t>> blobs;
    /// Keys named by the publish in flight, then by the last two
    /// publishes. Anything older is dropped: a scene's textures are
    /// bounded, but a session's history is not. Two generations of
    /// grace so a viewer still fetching for the previous snapshot
    /// while a new one is published does not race into a 404.
    std::set<std::string> pendingKeys, currentKeys, previousKeys;

    /// Bounds on one POST /blobs request, which is otherwise an
    /// unauthenticated allocation the client controls.
    static const size_t kMaxBatchKeys = 512;
    static const size_t kMaxBatchBody = kMaxBatchKeys * 64;

    void addBlob(const std::string &key, std::vector<uint8_t> &&data)
    {
        std::lock_guard<std::mutex> guard(mutex);
        pendingKeys.insert(key);
        // Content addressed: an existing entry is already this payload.
        blobs.emplace(key, std::move(data));
    }

    /// Name a stored blob as still in use by the publish in flight,
    /// without re-sending its bytes — what lets the publisher answer
    /// from its cacheId memo instead of re-serializing a mesh. Returns
    /// false when the blob is gone, which is the memo's invalidation:
    /// the caller then rebuilds and stores it.
    bool retainBlob(const std::string &key, uint32_t *size)
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = blobs.find(key);
        if (it == blobs.end())
            return false;
        pendingKeys.insert(key);
        if (size)
            *size = uint32_t(it->second.size());
        return true;
    }

    /// Roll the generations and drop what neither of them names.
    /// Called with \a mutex held, from publish().
    void retireBlobs()
    {
        previousKeys = std::move(currentKeys);
        currentKeys = std::move(pendingKeys);
        pendingKeys.clear();
        for (auto it = blobs.begin(); it != blobs.end();) {
            if (currentKeys.count(it->first) || previousKeys.count(it->first))
                ++it;
            else
                it = blobs.erase(it);
        }
    }

    std::mutex handlerMutex;
    std::function<void(const ScenePickRequest &)> pickHandler;

    /// One live WebSocket connection, registered by its wsLoop. All
    /// sends stay on that loop's thread: control messages are queued
    /// here and drained by the loop within its poll interval.
    struct Conn {
        int fd = -1;
        /// The scene version this connection has been given, seeded
        /// with what the client said it held on the upgrade request.
        /// Touched only by this connection's own push loop.
        uint64_t sent = 0;
        std::vector<std::string> pendingText; ///< queued control JSONs
        bool viewer = false;   ///< sent a hello — answers control requests
        std::string build;     ///< bundle build stamp from the hello
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
    std::condition_variable dumpCv;    ///< guarded by connMutex
    /// In-flight dumpFrame collection (one at a time).
    struct DumpCollect {
        uint32_t id = 0;
        size_t expected = 0;
        std::vector<ViewerFrameDump> dumps;
    };
    DumpCollect *dumpCollect = nullptr;
    uint32_t dumpIdCounter = 0;

    void broadcastControl(const std::string &json)
    {
        std::lock_guard<std::mutex> guard(connMutex);
        for (Conn *conn : conns) {
            if (conn->viewer)
                conn->pendingText.push_back(json);
        }
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

    void dispatchPick(const ScenePickRequest &req)
    {
        std::function<void(const ScenePickRequest &)> handler;
        {
            std::lock_guard<std::mutex> guard(handlerMutex);
            handler = pickHandler;
        }
        if (handler)
            handler(req);
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

        // /scene?v=<version>&s=<session>: what the client holds. 204
        // while it is current. A version stated without a session, or
        // with one from another run of this backend, names a publish
        // that never happened here and is worth nothing (SceneDump.h,
        // v35) — the client is treated as holding nothing.
        uint64_t clientVersion = ~uint64_t(0);
        std::string v = queryValue(query, "v");
        if (!v.empty())
            clientVersion = std::strtoull(v.c_str(), nullptr, 10);
        std::string s = queryValue(query, "s");
        if (!s.empty() && clientVersion != ~uint64_t(0)) {
            std::lock_guard<std::mutex> guard(mutex);
            if (std::strtoull(s.c_str(), nullptr, 10) != ensureSession())
                clientVersion = 0;
        }
        if (path != "/scene" && path != "/scene.fcsd") {
            static const char notFound[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(fd, notFound, sizeof(notFound) - 1);
            return;
        }

        // WebSocket upgrade: handshake, then stay in the push loop.
        std::string wsKey = headerValue(req, "sec-websocket-key");
        if (!wsKey.empty()) {
            if (handshake(fd, wsKey))
                wsLoop(fd, clientVersion);
            return;
        }

        std::vector<uint8_t> body;
        {
            std::lock_guard<std::mutex> guard(mutex);
            std::vector<uint8_t> out = payloadFor(clientVersion);
            if (out.empty()) {
                static const char noContent[] =
                    "HTTP/1.1 204 No Content\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Connection: close\r\n\r\n";
                sendAll(fd, noContent, sizeof(noContent) - 1);
                return;
            }
            body.resize(8 + out.size());
            uint64_t v = version;
            std::memcpy(body.data(), &v, 8);
            std::memcpy(body.data() + 8, out.data(), out.size());
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
    /// so.
    void wsLoop(int fd, uint64_t held)
    {
        Conn conn;
        conn.fd = fd;
        conn.sent = held;
        {
            std::lock_guard<std::mutex> guard(connMutex);
            conns.push_back(&conn);
        }
        wsLoopBody(fd, conn);
        {
            std::lock_guard<std::mutex> guard(connMutex);
            conns.erase(std::find(conns.begin(), conns.end(), &conn));
            // A collection waiting on this viewer would otherwise sit
            // out its full timeout.
            dumpCv.notify_all();
        }
    }

    void wsLoopBody(int fd, Conn &conn)
    {
        uint64_t &sent = conn.sent;
        std::string inbuf;
        int stampTick = 0;
        for (;;) {
            // Live bundle-stamp check (~every 5s at the 200ms poll):
            // a WASM rebuild while this backend serves pushes the
            // reload to already-connected pages, not just fresh hellos.
            if (++stampTick >= 25) {
                stampTick = 0;
                if (conn.viewer)
                    pushReloadIfStale(conn);
            }
            std::vector<uint8_t> body;
            {
                std::lock_guard<std::mutex> guard(mutex);
                // What this connection is missing, which after a
                // coalesced tick may be several publishes rather than
                // one — the push loop sends the current scene, not
                // every version of it.
                std::vector<uint8_t> out = payloadFor(sent);
                if (!out.empty()) {
                    body.resize(8 + out.size());
                    uint64_t v = version;
                    std::memcpy(body.data(), &v, 8);
                    std::memcpy(body.data() + 8, out.data(), out.size());
                    sent = version;
                }
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
            if (json.find("\"cmd\":\"hello\"") != std::string::npos) {
                {
                    std::lock_guard<std::mutex> guard(connMutex);
                    conn.viewer = true;
                    jsonStr(json, "build", conn.build);
                }
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
        if (size > 0 && bytes[0] == 'D') {
            handleFrameDump(bytes, size);
            return;
        }
        std::vector<uint8_t> data(bytes, bytes + size);
        handleEvent(data);
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

    void handleEvent(const std::vector<uint8_t> &data)
    {
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
            dispatchPick(req);
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
                    dispatchPick(req);
                    off += stride;
                }
            }
        }
    }
#else
    bool start(int) { return false; }
#endif
};

SceneStreamServer &SceneStreamServer::instance()
{
    static SceneStreamServer server;
    return server;
}

SceneStreamServer::Private *SceneStreamServer::ensure()
{
    if (!pimpl)
        pimpl = new Private;
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

uint64_t SceneStreamServer::sessionId()
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->mutex);
    return p->ensureSession();
}

uint64_t SceneStreamServer::beginPublish(const void *publisher)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->mutex);
    p->ensureSession();
    if (!p->publisher)
        p->publisher = publisher;
    else if (p->publisher != publisher)
        return 0;
    // Handed out before the payload exists, so a serializer that fails
    // leaves a gap. Versions are monotonic, not gapless: a consumer
    // compares them, it does not count them.
    return ++p->handedOut;
}

void SceneStreamServer::endPublish(const void *publisher)
{
    if (!pimpl)
        return;
    std::lock_guard<std::mutex> guard(pimpl->mutex);
    if (pimpl->publisher == publisher)
        pimpl->publisher = nullptr;
}

void SceneStreamServer::publish(ScenePublish &&pub)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->mutex);
    if (pub.version <= p->version)
        return;   // superseded before it was installed
    // A gap would make the history lie about what a viewer is missing:
    // merging across it would skip whatever the missing publish said.
    if (!p->history.empty()
            && p->history.back().version + 1 != pub.version)
        p->history.clear();
    p->history.push_back({pub.version, std::move(pub.changed),
                          std::move(pub.removed)});
    while (p->history.size() > Private::kHistory)
        p->history.pop_front();

    p->payload = std::move(pub.payload);
    p->spans = pub.spans;
    // Version and payload move together, and only here: what is served
    // must always be the bytes the version names.
    p->version = pub.version;
    p->retireBlobs();
}

void SceneStreamServer::publishBlob(const std::string &key,
                                    std::vector<uint8_t> &&data)
{
    ensure()->addBlob(key, std::move(data));
}

bool SceneStreamServer::retainBlob(const std::string &key, uint32_t *size)
{
    return ensure()->retainBlob(key, size);
}

void SceneStreamServer::setPickHandler(
        std::function<void(const ScenePickRequest &)> handler)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->handlerMutex);
    p->pickHandler = std::move(handler);
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
