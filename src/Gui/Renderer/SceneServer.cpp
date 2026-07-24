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
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
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
    uint64_t version = 0;
    int listenFd = -1;
    bool started = false;

    std::mutex handlerMutex;
    std::function<void(const ScenePickRequest &)> pickHandler;

    /// One live WebSocket connection, registered by its wsLoop. All
    /// sends stay on that loop's thread: control messages are queued
    /// here and drained by the loop within its poll interval.
    struct Conn {
        int fd = -1;
        std::vector<std::string> pendingText; ///< queued control JSONs
        bool viewer = false;   ///< sent a hello — answers control requests
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
        // Read the request head (the viewer sends no body).
        std::string req;
        char buf[1024];
        while (req.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
                return;
            req.append(buf, size_t(n));
            if (req.size() > 16384)
                return;
        }
        if (req.compare(0, 4, "GET ") != 0)
            return;
        std::string path = req.substr(4, req.find(' ', 4) - 4);

        // /scene?v=<version>: 204 while the client is current.
        uint64_t clientVersion = ~uint64_t(0);
        auto q = path.find("?v=");
        if (q != std::string::npos) {
            clientVersion = std::strtoull(path.c_str() + q + 3,
                                          nullptr, 10);
            path.resize(q);
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
                wsLoop(fd);
            return;
        }

        std::vector<uint8_t> body;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (version == clientVersion || payload.empty()) {
                static const char noContent[] =
                    "HTTP/1.1 204 No Content\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Connection: close\r\n\r\n";
                sendAll(fd, noContent, sizeof(noContent) - 1);
                return;
            }
            body.resize(8 + payload.size());
            uint64_t v = version;
            std::memcpy(body.data(), &v, 8);
            std::memcpy(body.data() + 8, payload.data(), payload.size());
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
    void wsLoop(int fd)
    {
        Conn conn;
        conn.fd = fd;
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
        uint64_t sent = 0;
        std::string inbuf;
        for (;;) {
            std::vector<uint8_t> body;
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (version != sent && !payload.empty()) {
                    body.resize(8 + payload.size());
                    uint64_t v = version;
                    std::memcpy(body.data(), &v, 8);
                    std::memcpy(body.data() + 8, payload.data(),
                                payload.size());
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
                }
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

void SceneStreamServer::publish(std::vector<uint8_t> &&payload)
{
    Private *p = ensure();
    std::lock_guard<std::mutex> guard(p->mutex);
    p->payload = std::move(payload);
    ++p->version;
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
