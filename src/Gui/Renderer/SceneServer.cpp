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

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace Render;

class SceneStreamServer::Private {
public:
    std::mutex mutex;
    std::vector<uint8_t> payload;
    uint64_t version = 0;
    int listenFd = -1;

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
            handle(fd);
            ::close(fd);
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
        // Read the request head (we only care about the request line;
        // the viewer sends no body).
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
#else
    bool start(int) { return false; }
#endif
};

SceneStreamServer &SceneStreamServer::instance()
{
    static SceneStreamServer server;
    return server;
}

bool SceneStreamServer::start(int port)
{
    if (pimpl)
        return running();
    pimpl = new Private;
    if (!pimpl->start(port)) {
        delete pimpl;
        pimpl = nullptr;
        return false;
    }
    return true;
}

bool SceneStreamServer::running() const
{
    return pimpl != nullptr;
}

void SceneStreamServer::publish(std::vector<uint8_t> &&payload)
{
    if (!pimpl)
        return;
    std::lock_guard<std::mutex> guard(pimpl->mutex);
    pimpl->payload = std::move(payload);
    ++pimpl->version;
}
