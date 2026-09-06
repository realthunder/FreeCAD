// SPDX-License-Identifier: LGPL-2.1-or-later

/// The scene server over a real socket (docs/SceneServerPort.md stage
/// 0). Every earlier test drives the server's machinery directly and
/// says so ("no sockets"); this one starts the listener on a port and
/// speaks to it as a viewer would: the HTTP routes, the WebSocket
/// handshake, the hello and the snapshot push, a control op, a binary
/// push, fragmentation, a ping, the door refusing a bad token, a kick,
/// a document switch, and a server stop.
///
/// The client side is Boost.Beast, deliberately: a handshake accepted
/// by an independent RFC 6455 implementation is evidence a hand-rolled
/// client that mirrors our own parser could not give, and the same
/// client will speak to the ported server unchanged -- which is the
/// whole point of the stage: the wire must not move. It is also the
/// first Beast code compiled in this tree. Portable by construction;
/// on a platform where the listener is still the stub, the failure
/// here is the truthful verdict, not noise.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <Gui/Renderer/SceneDump.h>
#include <Gui/Renderer/SceneServer.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace
{

/// A port nothing is listening on: let the kernel pick one and hand it
/// straight back. Racy in principle; in a test process that binds it
/// again milliseconds later, good enough, and far better than a fixed
/// number colliding with the user's serving rig.
int freePort()
{
    net::io_context ioc;
    tcp::acceptor a(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
    return a.local_endpoint().port();
}

/// Connect to the server, retrying while its accept thread is still
/// getting to its feet.
void connectWithRetry(tcp::socket& sock, int port, const char* host = "127.0.0.1")
{
    const tcp::endpoint ep(net::ip::make_address(host), uint16_t(port));
    beast::error_code ec;
    for (int i = 0; i < 300; ++i) {
        sock.connect(ep, ec);
        if (!ec) {
            return;
        }
        sock.close(ec);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("scene server never accepted a connection");
}

/// Poll \a pred every few milliseconds for up to \a ms.
bool waitFor(const std::function<bool()>& pred, int ms = 3000)
{
    for (int i = 0; i < ms / 5; ++i) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

struct HttpReply
{
    bool ok = false;
    unsigned status = 0;
    std::string body;
    http::response<http::string_body> res;
};

/// One HTTP exchange on a fresh connection, the way the polling
/// fallback and the blob fetches use the server.
HttpReply httpRequest(int port, http::verb verb, const std::string& target,
                      const std::string& body = {}, bool chunked = false)
{
    HttpReply out;
    net::io_context ioc;
    tcp::socket sock(ioc);
    connectWithRetry(sock, port);
    http::request<http::string_body> req {verb, target, 11};
    req.set(http::field::host, "localhost");
    req.set(http::field::connection, "close");
    if (!body.empty()) {
        req.body() = body;
        // chunked(true) is Transfer-Encoding: chunked and no
        // Content-Length; the serializer writes the chunks.
        if (chunked) {
            req.chunked(true);
        }
        else {
            req.prepare_payload();
        }
    }
    beast::error_code ec;
    http::write(sock, req, ec);
    if (ec) {
        return out;
    }
    beast::flat_buffer buf;
    http::read(sock, buf, out.res, ec);
    if (ec) {
        return out;
    }
    out.ok = true;
    out.status = out.res.result_int();
    out.body = out.res.body();
    return out;
}

/// One viewer connection: a Beast WebSocket client with reads that
/// give up instead of hanging the test. A read that gives up stays
/// pending rather than being cancelled -- Beast treats a cancelled
/// operation as the end of the stream -- so a probe for "nothing
/// arrives" can be followed by more traffic, and the next read simply
/// picks the pending one up.
class WsClient
{
public:
    struct Msg
    {
        bool ok = false;
        bool text = false;
        std::string data;
        beast::error_code ec;
    };

    using Headers = std::vector<std::pair<std::string, std::string>>;

    /// \a headers ride the upgrade request, the way a proxy's
    /// forwarded-address and identity headers do; \a host is the
    /// address to connect to.
    WsClient(int port, const std::string& target, Headers headers = {},
             const char* host = "127.0.0.1")
        : ws(ioc)
    {
        connectWithRetry(ws.next_layer(), port, host);
        ws.control_callback([this](websocket::frame_type t, beast::string_view s) {
            control.emplace_back(t, std::string(s));
        });
        if (!headers.empty()) {
            ws.set_option(websocket::stream_base::decorator(
                [headers](websocket::request_type& r) {
                    for (const auto& h : headers) {
                        r.set(h.first, h.second);
                    }
                }));
        }
        ws.handshake("localhost", target);
    }

    ~WsClient()
    {
        if (pending) {
            beast::error_code ignore;
            ws.next_layer().close(ignore);
            ioc.restart();
            ioc.run();
        }
    }

    Msg read(int ms = 5000)
    {
        if (!pending) {
            pending = true;
            done = false;
            ws.async_read(buf, [this](beast::error_code ec, std::size_t) {
                done = true;
                readEc = ec;
            });
        }
        ioc.restart();
        ioc.run_for(std::chrono::milliseconds(ms));
        Msg m;
        if (!done) {
            m.ec = net::error::timed_out;
            return m;
        }
        pending = false;
        m.ec = readEc;
        if (m.ec) {
            return m;
        }
        m.ok = true;
        m.text = ws.got_text();
        m.data = beast::buffers_to_string(buf.data());
        buf.consume(buf.size());
        return m;
    }

    /// The next message, skipping any text that is not the one asked
    /// for -- the server volunteers a docs listing on a serve or
    /// unserve, and a test that did not cause one must not trip on it.
    Msg readBinary(int ms = 5000)
    {
        for (int i = 0; i < 4; ++i) {
            Msg m = read(ms);
            if (!m.ok || !m.text) {
                return m;
            }
        }
        Msg m;
        m.ec = net::error::timed_out;
        return m;
    }

    void sendText(const std::string& s)
    {
        ws.text(true);
        ws.write(net::buffer(s));
    }

    void sendBinary(const std::vector<uint8_t>& b)
    {
        ws.binary(true);
        ws.write(net::buffer(b));
    }

    void hello(const std::string& client, const std::string& extra = {})
    {
        sendText("{\"cmd\":\"hello\",\"client\":\"" + client + "\",\"snapshot\":"
                 + std::to_string(Render::sceneDumpVersion()) + extra + "}");
    }

    net::io_context ioc;
    websocket::stream<tcp::socket> ws;
    std::vector<std::pair<websocket::frame_type, std::string>> control;

private:
    beast::flat_buffer buf;
    bool pending = false;
    bool done = false;
    beast::error_code readEc;
};

/// Text-frame errors and replies are matched on their JSON fragments,
/// the way the server itself matches verbs.
bool has(const std::string& json, const char* fragment)
{
    return json.find(fragment) != std::string::npos;
}

/// The wire's binary scene message: 8-byte little-endian version,
/// then the payload the publisher handed in.
std::vector<uint8_t> versioned(uint64_t version, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> out(8 + payload.size());
    std::memcpy(out.data(), &version, 8);
    std::memcpy(out.data() + 8, payload.data(), payload.size());
    return out;
}

std::string asString(const std::vector<uint8_t>& v)
{
    return std::string(v.begin(), v.end());
}

/// Publish \a payload as the next version of \a doc, from a publisher
/// identity private to this file.
uint64_t publishBytes(const std::vector<uint8_t>& payload, const std::string& doc = {})
{
    static int tag;
    auto& server = Render::SceneStreamServer::instance();
    const uint64_t version = server.beginPublish(&tag, doc);
    if (!version) {
        return 0;
    }
    Render::SceneStreamServer::ScenePublish pub;
    pub.version = version;
    pub.payload = payload;
    pub.objects = 1;
    server.publish(std::move(pub), doc);
    return version;
}

/// A payload long enough for the 16-bit frame length, and unlike
/// anything else on the wire.
std::vector<uint8_t> makePayload(uint8_t seed, size_t size = 1000)
{
    std::vector<uint8_t> out(size);
    for (size_t i = 0; i < size; ++i) {
        out[i] = uint8_t(seed + i * 7);
    }
    return out;
}

/// Roster entry of the connection that said hello as \a label.
bool findClient(const std::string& label, Render::SceneClientInfo& info)
{
    std::vector<Render::SceneClientInfo> list;
    Render::SceneStreamServer::instance().clients(list);
    for (const auto& c : list) {
        if (c.client == label) {
            info = c;
            return true;
        }
    }
    return false;
}

class SceneServerWire: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        auto& server = Render::SceneStreamServer::instance();
        // The environment must not gate this run: FC_SERVE_TOKEN in the
        // user's shell would turn every open-door case into a 403.
        server.setToken({});
        server.setTrustProxy(false);
        port = freePort();
        ASSERT_GT(port, 0);
        ASSERT_TRUE(server.start(port)) << "the listener did not start";
        ASSERT_TRUE(server.running());
        payload = makePayload(1);
        version = publishBytes(payload);
        ASSERT_GT(version, 0u);
        session = server.sessionId();
    }

    static int port;
    static std::vector<uint8_t> payload;
    static uint64_t version;
    static uint64_t session;
};

int SceneServerWire::port = 0;
std::vector<uint8_t> SceneServerWire::payload;
uint64_t SceneServerWire::version = 0;
uint64_t SceneServerWire::session = 0;

}  // namespace

TEST_F(SceneServerWire, httpSceneRoute)
{
    HttpReply r = httpRequest(port, http::verb::get, "/scene?v=0");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 200u) << "a viewer holding nothing gets the scene";
    EXPECT_EQ(r.body, asString(versioned(version, payload)));
    EXPECT_EQ(r.res[http::field::access_control_allow_origin], "*");

    r = httpRequest(port, http::verb::get,
                    "/scene?v=" + std::to_string(version) + "&s=" + std::to_string(session));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 204u) << "a viewer that is current gets nothing";

    r = httpRequest(port, http::verb::get, "/scene?v=" + std::to_string(version));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 200u) << "a version without a session names nothing (v35)";

    r = httpRequest(port, http::verb::get, "/scene?v=0&doc=NoSuchDocument");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 404u);

    r = httpRequest(port, http::verb::get, "/nothing-here");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 404u);
}

TEST_F(SceneServerWire, httpBlobRoutes)
{
    auto& server = Render::SceneStreamServer::instance();
    const std::string key(40, 'a');
    const std::vector<uint8_t> blob = makePayload(9, 300);
    server.publishBlob(key, std::vector<uint8_t>(blob));

    HttpReply r = httpRequest(port, http::verb::get, "/blob?key=" + key);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 200u);
    EXPECT_EQ(r.body, asString(blob));
    EXPECT_NE(std::string(r.res[http::field::cache_control]).find("immutable"),
              std::string::npos)
        << "content addressed, so cacheable forever";

    r = httpRequest(port, http::verb::get, "/blob?key=" + std::string(40, 'b'));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 404u) << "an unknown key";
    r = httpRequest(port, http::verb::get, "/blob?key=nope");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 404u) << "a malformed key never reaches the store";

    // The batch: 'FCBB', u32 count, then per key 40 raw bytes, u32
    // length (0xffffffff = unknown), payload.
    const std::string unknown(40, 'c');
    r = httpRequest(port, http::verb::post, "/blobs", key + "\n" + unknown + "\n");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 200u);
    ASSERT_GE(r.body.size(), 8u + 2 * 44 + blob.size());
    EXPECT_EQ(r.body.substr(0, 4), "FCBB");
    uint32_t count = 0;
    std::memcpy(&count, r.body.data() + 4, 4);
    EXPECT_EQ(count, 2u);
    size_t off = 8;
    EXPECT_EQ(r.body.substr(off, 40), key);
    uint32_t len = 0;
    std::memcpy(&len, r.body.data() + off + 40, 4);
    EXPECT_EQ(len, blob.size());
    EXPECT_EQ(r.body.substr(off + 44, blob.size()), asString(blob));
    off += 44 + blob.size();
    EXPECT_EQ(r.body.substr(off, 40), unknown);
    std::memcpy(&len, r.body.data() + off + 40, 4);
    EXPECT_EQ(len, 0xffffffffu);
    EXPECT_EQ(r.body.size(), off + 44);
}

TEST_F(SceneServerWire, handshakeThenHelloThenSnapshot)
{
    // Beast validates the 101, the Upgrade/Connection headers and the
    // Sec-WebSocket-Accept digest itself; a wrong one throws here.
    WsClient c(port, "/scene");
    c.hello("wire-snapshot");

    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_FALSE(m.text);
    EXPECT_EQ(m.data, asString(versioned(version, payload)))
        << "the first push is the versioned payload, byte for byte";

    Render::SceneClientInfo info;
    ASSERT_TRUE(waitFor([&] { return findClient("wire-snapshot", info); }));
    EXPECT_TRUE(info.viewer) << "a hello makes a viewer";
    EXPECT_FALSE(info.viewOnly);
    EXPECT_NE(info.peer.find("127.0.0.1:"), std::string::npos);
    EXPECT_EQ(info.address, info.peer) << "no proxy, so the judged address is the peer";
    EXPECT_FALSE(info.proxied);
    EXPECT_TRUE(info.identity.empty());

    c.sendText("{\"cmd\":\"docs\"}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_TRUE(m.text);
    EXPECT_TRUE(has(m.data, "{\"cmd\":\"docs\",\"list\":[")) << m.data;

    // A republish reaches the open connection, as the new version.
    const std::vector<uint8_t> next = makePayload(2, 70000);
    const uint64_t v2 = publishBytes(next);
    ASSERT_GT(v2, version);
    m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, asString(versioned(v2, next)))
        << "a 64-bit frame length, and the newest version";
    // Leave the suite's payload current for the cases that follow.
    version = publishBytes(payload);
    m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, asString(versioned(version, payload)));
}

TEST_F(SceneServerWire, heldVersionOnTheUpgradeCostsNoPayload)
{
    // A reconnecting viewer states what it holds on the upgrade request
    // and, being current, must get no snapshot -- so the very next
    // message after its hello is the docs listing it asks for.
    WsClient c(port,
               "/scene?v=" + std::to_string(version) + "&s=" + std::to_string(session));
    c.hello("wire-held");
    c.sendText("{\"cmd\":\"docs\"}");
    WsClient::Msg m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_TRUE(m.text) << "a snapshot was pushed to a viewer that was current";
    EXPECT_TRUE(has(m.data, "\"cmd\":\"docs\"")) << m.data;

    // A resync asks for the scene again, over this socket.
    c.sendText("{\"cmd\":\"resync\"}");
    m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, asString(versioned(version, payload)));
}

TEST_F(SceneServerWire, controlOpReachesTheHandlerAndItsReplyComesBack)
{
    auto& server = Render::SceneStreamServer::instance();
    std::mutex mutex;
    std::vector<std::string> seen;
    uint64_t from = 0;
    bool viewOnly = true;
    server.setControlHandler([&](Render::SceneControlRequest&& req) {
        {
            std::lock_guard<std::mutex> guard(mutex);
            seen.push_back(req.json);
            from = req.client;
            viewOnly = req.viewOnly;
        }
        req.reply("{\"id\":7,\"ok\":true,\"echo\":\"answered\"}");
    });

    WsClient c(port, "/scene");
    c.hello("wire-op");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();

    c.sendText("{\"op\":\"probe\",\"id\":7}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_TRUE(m.text);
    EXPECT_EQ(m.data, "{\"id\":7,\"ok\":true,\"echo\":\"answered\"}");
    {
        std::lock_guard<std::mutex> guard(mutex);
        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen[0], "{\"op\":\"probe\",\"id\":7}");
        EXPECT_FALSE(viewOnly);
    }
    Render::SceneClientInfo info;
    ASSERT_TRUE(findClient("wire-op", info));
    EXPECT_EQ(from, info.id) << "the request names the connection it came on";

    // A mode change is announced to the client, and then carried on
    // every request, for the layer that knows which ops write.
    ASSERT_TRUE(server.setClientViewOnly(info.id, true));
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, "{\"cmd\":\"config\",\"viewOnly\":true}");
    c.sendText("{\"op\":\"probe\",\"id\":8}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, "{\"id\":7,\"ok\":true,\"echo\":\"answered\"}");
    {
        std::lock_guard<std::mutex> guard(mutex);
        ASSERT_EQ(seen.size(), 2u);
        EXPECT_TRUE(viewOnly);
    }

    // No handler: a structured refusal correlated on the id, never
    // silence.
    server.setControlHandler(nullptr);
    c.sendText("{\"op\":\"probe\",\"id\":9}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, "{\"id\":9,\"ok\":false,\"code\":\"NoHandler\"}");
}

TEST_F(SceneServerWire, textAndBinaryPushesArriveInOrder)
{
    auto& server = Render::SceneStreamServer::instance();
    WsClient c(port, "/scene");
    c.hello("wire-push");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    Render::SceneClientInfo info;
    ASSERT_TRUE(findClient("wire-push", info));

    // From a foreign thread, as the Cycles frame stream does it.
    const std::vector<uint8_t> frame = makePayload(5, 70000);
    std::thread([&] {
        EXPECT_TRUE(server.sendControl(info.id, "{\"cmd\":\"note\",\"n\":1}"));
        EXPECT_TRUE(server.sendControl(info.id, "{\"cmd\":\"note\",\"n\":2}"));
        EXPECT_TRUE(server.sendBinary(info.id, std::vector<uint8_t>(frame)));
    }).join();

    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_TRUE(m.text);
    EXPECT_EQ(m.data, "{\"cmd\":\"note\",\"n\":1}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, "{\"cmd\":\"note\",\"n\":2}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_FALSE(m.text);
    EXPECT_EQ(m.data, asString(frame)) << "the binary rides after the queued texts";

    EXPECT_FALSE(server.sendControl(info.id + 1000, "{}")) << "an unknown connection";
    EXPECT_FALSE(server.sendBinary(info.id + 1000, {1, 2, 3}));
}

TEST_F(SceneServerWire, fragmentedMessagesAreReassembled)
{
    auto& server = Render::SceneStreamServer::instance();
    std::mutex mutex;
    std::vector<Render::ScenePickRequest> picks;
    std::vector<std::string> ops;
    server.setPickHandler([&](const Render::ScenePickRequest& req) {
        std::lock_guard<std::mutex> guard(mutex);
        picks.push_back(req);
    });
    server.setControlHandler([&](Render::SceneControlRequest&& req) {
        std::lock_guard<std::mutex> guard(mutex);
        ops.push_back(req.json);
    });

    WsClient c(port, "/scene");
    c.hello("wire-frag");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();

    // A pick, 'P' + flags + six floats, split across two binary frames.
    std::vector<uint8_t> pick(2 + 6 * sizeof(float));
    pick[0] = 'P';
    pick[1] = 1;
    const float ray[6] = {1.5f, -2.5f, 3.5f, 0.f, 0.6f, -0.8f};
    std::memcpy(pick.data() + 2, ray, sizeof(ray));
    c.ws.binary(true);
    c.ws.write_some(false, net::buffer(pick.data(), 9));
    c.ws.write_some(true, net::buffer(pick.data() + 9, pick.size() - 9));
    ASSERT_TRUE(waitFor([&] {
        std::lock_guard<std::mutex> guard(mutex);
        return !picks.empty();
    })) << "the fragmented pick never reached the handler";
    {
        std::lock_guard<std::mutex> guard(mutex);
        ASSERT_EQ(picks.size(), 1u);
        EXPECT_EQ(picks[0].modifiers, 1u);
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(picks[0].origin[i], ray[i]);
            EXPECT_EQ(picks[0].dir[i], ray[3 + i]);
        }
    }

    // A control op in three text fragments.
    const std::string op = "{\"op\":\"select\",\"id\":11,\"path\":\"Body.Pad\"}";
    c.ws.text(true);
    c.ws.write_some(false, net::buffer(op.data(), 5));
    c.ws.write_some(false, net::buffer(op.data() + 5, 12));
    c.ws.write_some(true, net::buffer(op.data() + 17, op.size() - 17));
    ASSERT_TRUE(waitFor([&] {
        std::lock_guard<std::mutex> guard(mutex);
        return !ops.empty();
    })) << "the fragmented op never reached the handler";
    {
        std::lock_guard<std::mutex> guard(mutex);
        ASSERT_EQ(ops.size(), 1u);
        EXPECT_EQ(ops[0], op);
    }

    // A view-only connection's picks are dropped, not refused.
    Render::SceneClientInfo info;
    ASSERT_TRUE(findClient("wire-frag", info));
    ASSERT_TRUE(server.setClientViewOnly(info.id, true));
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_TRUE(has(m.data, "\"viewOnly\":true")) << m.data;
    c.sendBinary(pick);
    c.sendText("{\"cmd\":\"docs\"}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_TRUE(has(m.data, "\"cmd\":\"docs\"")) << m.data;
    {
        std::lock_guard<std::mutex> guard(mutex);
        EXPECT_EQ(picks.size(), 1u) << "a view-only pick was dispatched";
    }

    server.setPickHandler(nullptr);
    server.setControlHandler(nullptr);
}

TEST_F(SceneServerWire, pingIsAnsweredWithItsPayload)
{
    WsClient c(port, "/scene");
    c.hello("wire-ping");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();

    c.ws.ping(websocket::ping_data("keepalive"));
    c.sendText("{\"cmd\":\"docs\"}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    ASSERT_EQ(c.control.size(), 1u) << "exactly one pong, before the docs reply";
    EXPECT_EQ(c.control[0].first, websocket::frame_type::pong);
    EXPECT_EQ(c.control[0].second, "keepalive");
}

TEST_F(SceneServerWire, theDoorRefusesABadTokenBeforeAnySceneBytes)
{
    auto& server = Render::SceneStreamServer::instance();
    server.setToken("s3cret");
    struct Open
    {
        ~Open()
        {
            Render::SceneStreamServer::instance().setToken({});
        }
    } open;

    HttpReply r = httpRequest(port, http::verb::get, "/scene?v=0");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 403u) << "every HTTP route is gated";
    r = httpRequest(port, http::verb::get, "/scene?v=0&token=wrong");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 403u);
    r = httpRequest(port, http::verb::get, "/scene?v=0&token=s3cret");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 200u);
    EXPECT_EQ(r.body, asString(versioned(version, payload)));

    {
        // An upgrade without the token still handshakes: the token may
        // come in the hello. Until it does, no verb works and nothing
        // is pushed.
        WsClient c(port, "/scene");
        c.sendText("{\"cmd\":\"docs\"}");
        WsClient::Msg m = c.read(700);
        EXPECT_EQ(m.ec, net::error::timed_out) << "unauthorized, yet answered: " << m.data;

        c.hello("wire-door", ",\"token\":\"wrong\"");
        m = c.read();
        ASSERT_TRUE(m.ok) << m.ec.message();
        EXPECT_TRUE(m.text);
        EXPECT_EQ(m.data, "{\"cmd\":\"error\",\"code\":\"BadToken\"}");
        // A refusal rides the kick path: the reason, the farewell every
        // kicked connection gets, then the close.
        m = c.read();
        ASSERT_TRUE(m.ok) << m.ec.message();
        EXPECT_EQ(m.data, "{\"cmd\":\"error\",\"code\":\"Kicked\"}");
        m = c.read();
        EXPECT_FALSE(m.ok) << "still open after a refusal: " << m.data;
        EXPECT_NE(m.ec, net::error::timed_out) << "told, then hung up -- not just quiet";
    }
    {
        // The token in the hello opens the door.
        WsClient c(port, "/scene");
        c.hello("wire-door-late", ",\"token\":\"s3cret\"");
        WsClient::Msg m = c.readBinary();
        ASSERT_TRUE(m.ok) << m.ec.message();
        EXPECT_EQ(m.data, asString(versioned(version, payload)));
    }
    {
        // The token on the upgrade opens it before the hello.
        WsClient c(port, "/scene?token=s3cret");
        c.hello("wire-door-early");
        WsClient::Msg m = c.readBinary();
        ASSERT_TRUE(m.ok) << m.ec.message();
        EXPECT_EQ(m.data, asString(versioned(version, payload)));
    }
}

TEST_F(SceneServerWire, aKickIsToldThenClosed)
{
    auto& server = Render::SceneStreamServer::instance();
    WsClient c(port, "/scene");
    c.hello("wire-kick");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    Render::SceneClientInfo info;
    ASSERT_TRUE(findClient("wire-kick", info));

    ASSERT_TRUE(server.kickClient(info.id));
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, "{\"cmd\":\"error\",\"code\":\"Kicked\"}");
    m = c.read();
    EXPECT_FALSE(m.ok);
    EXPECT_NE(m.ec, net::error::timed_out) << "kicked, but the socket stayed up";

    EXPECT_TRUE(waitFor([&] { return !findClient("wire-kick", info); }))
        << "the roster still lists a kicked connection";
    EXPECT_FALSE(server.kickClient(info.id)) << "gone is gone";
}

TEST_F(SceneServerWire, documentsJoinSwitchAndReHome)
{
    auto& server = Render::SceneStreamServer::instance();
    const std::vector<uint8_t> wirePayload = makePayload(3, 500);
    server.setDocumentInfo("Wire", "Wire label");
    struct Release
    {
        ~Release()
        {
            Render::SceneStreamServer::instance().releaseGroup("Wire");
        }
    } release;
    const uint64_t wireVersion = publishBytes(wirePayload, "Wire");
    ASSERT_GT(wireVersion, 0u);

    WsClient c(port, "/scene");
    c.hello("wire-doc", ",\"doc\":\"Wire\"");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, asString(versioned(wireVersion, wirePayload)))
        << "a hello naming a document gets that document";

    c.sendText("{\"cmd\":\"docs\"}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_TRUE(has(m.data, "{\"name\":\"Wire\",\"label\":\"Wire label\",\"objects\":1}"))
        << m.data;
    EXPECT_TRUE(has(m.data, "\"default\":\"Wire\"")) << m.data;

    c.sendText("{\"cmd\":\"switch\",\"doc\":\"NoSuchDocument\"}");
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data,
              "{\"cmd\":\"error\",\"code\":\"UnknownDocument\",\"doc\":\"NoSuchDocument\"}");

    Render::SceneClientInfo info;
    ASSERT_TRUE(findClient("wire-doc", info));
    EXPECT_EQ(info.doc, "Wire") << "a failed switch leaves the connection where it was";

    // Unserving the document re-homes its viewers to the default one,
    // which they get in full.
    server.releaseGroup("Wire");
    m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, asString(versioned(version, payload)));
    EXPECT_TRUE(waitFor([&] { return findClient("wire-doc", info) && info.doc.empty(); }))
        << "the roster still shows the released document";
}

TEST_F(SceneServerWire, nothingWaitsForTheTick)
{
    // docs/SceneServerPort.md sec 7.3: a hello's snapshot, a publish and
    // a host push each reach the wire on their own wake. The 200 ms
    // tick is a fallback; under it each of these would wait anywhere up
    // to a tick, so eight in a row under half of one is (1/2)^8 of
    // luck, and on the wake it is a loopback round trip.
    using clock = std::chrono::steady_clock;
    auto since = [](clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    };
    const double kBudgetMs = 100.0;   // half the fallback tick

    auto& server = Render::SceneStreamServer::instance();
    WsClient c(port, "/scene");
    clock::time_point t0 = clock::now();
    c.hello("wire-latency");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    const double helloMs = since(t0);
    EXPECT_LT(helloMs, kBudgetMs) << "the snapshot after a hello waited for a tick";

    Render::SceneClientInfo info;
    ASSERT_TRUE(waitFor([&] { return findClient("wire-latency", info); }));

    double publishMs = 0;
    for (int i = 0; i < 8; ++i) {
        const std::vector<uint8_t> next = makePayload(uint8_t(10 + i), 5000);
        t0 = clock::now();
        const uint64_t v = publishBytes(next);
        ASSERT_GT(v, 0u);
        m = c.readBinary();
        ASSERT_TRUE(m.ok) << m.ec.message();
        ASSERT_EQ(m.data, asString(versioned(v, next)));
        publishMs = std::max(publishMs, since(t0));
    }
    EXPECT_LT(publishMs, kBudgetMs) << "a publish waited for a tick";

    double pushMs = 0;
    for (int i = 0; i < 8; ++i) {
        const std::string note = "{\"cmd\":\"note\",\"n\":" + std::to_string(i) + "}";
        t0 = clock::now();
        // From a foreign thread, as the host does it.
        std::thread([&] { EXPECT_TRUE(server.sendControl(info.id, note)); }).join();
        m = c.read();
        ASSERT_TRUE(m.ok) << m.ec.message();
        ASSERT_EQ(m.data, note);
        pushMs = std::max(pushMs, since(t0));
    }
    EXPECT_LT(pushMs, kBudgetMs) << "a host push waited for a tick";
    std::printf("[ latency  ] hello %.1f ms, publish max %.1f ms, host push max %.1f ms\n",
                helloMs, publishMs, pushMs);

    // Leave the suite's payload current for the cases that follow.
    version = publishBytes(payload);
    m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, asString(versioned(version, payload)));
}

TEST_F(SceneServerWire, listensOnIPv6Too)
{
    // Whether this host has IPv6 at all: a throwaway bind on ::1. A
    // host without it is served by the v4 fallback, which every other
    // case here exercises.
    net::io_context ioc;
    beast::error_code ec;
    {
        tcp::acceptor probe(ioc);
        probe.open(tcp::v6(), ec);
        if (!ec) {
            probe.bind(tcp::endpoint(net::ip::make_address("::1"), 0), ec);
        }
        if (ec) {
            GTEST_SKIP() << "no IPv6 on this host: " << ec.message();
        }
    }
    WsClient c(port, "/scene", {}, "::1");
    c.hello("wire-v6");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, asString(versioned(version, payload)));
    Render::SceneClientInfo info;
    ASSERT_TRUE(waitFor([&] { return findClient("wire-v6", info); }));
    EXPECT_EQ(info.peer.rfind("::1:", 0), 0u) << "a v6 peer shown as itself: " << info.peer;
    EXPECT_EQ(info.address, info.peer);
    // And the v4 peers of the same dual-stack listener are not shown
    // as v4-mapped v6 (handshakeThenHelloThenSnapshot checks the
    // 127.0.0.1 form, so this is the other half of that).
}

TEST_F(SceneServerWire, aChunkedPostBodyIsRead)
{
    auto& server = Render::SceneStreamServer::instance();
    const std::string key(40, 'd');
    const std::vector<uint8_t> blob = makePayload(3, 200);
    server.publishBlob(key, std::vector<uint8_t>(blob));

    // Transfer-Encoding: chunked, no Content-Length -- what a proxy
    // that re-frames bodies sends. The audit's Content-Length-only
    // reader would have read an empty body.
    HttpReply r = httpRequest(port, http::verb::post, "/blobs", key + "\n", true);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 200u);
    ASSERT_GE(r.body.size(), 8u + 44 + blob.size());
    EXPECT_EQ(r.body.substr(0, 4), "FCBB");
    uint32_t count = 0;
    std::memcpy(&count, r.body.data() + 4, 4);
    EXPECT_EQ(count, 1u) << "the chunked body reached the route whole";
    EXPECT_EQ(r.body.substr(8, 40), key);
    uint32_t len = 0;
    std::memcpy(&len, r.body.data() + 48, 4);
    EXPECT_EQ(len, blob.size());
}

TEST_F(SceneServerWire, anOversizeControlFrameEndsTheConnection)
{
    WsClient c(port, "/scene");
    c.hello("wire-control");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();

    // RFC 6455 5.5: a control frame carries at most 125 bytes. A
    // masked ping of 126 -- 0x89, mask bit | 126, u16 length, a zero
    // mask, the payload -- written under Beast's client, which would
    // refuse to send one itself.
    std::vector<uint8_t> frame = {0x89, 0xFE, 0x00, 126, 0, 0, 0, 0};
    frame.resize(8 + 126, 0x41);
    beast::error_code ec;
    net::write(c.ws.next_layer(), net::buffer(frame), ec);
    ASSERT_FALSE(ec) << ec.message();
    m = c.read();
    EXPECT_FALSE(m.ok) << "still open after a 126-byte ping: " << m.data;
    EXPECT_NE(m.ec, net::error::timed_out) << "closed, not merely quiet";
}

TEST_F(SceneServerWire, theCapCountsUsersNotAddresses)
{
    auto& server = Render::SceneStreamServer::instance();
    server.setTrustProxy(true);
    server.setConnectionCaps(3, 2);
    struct Restore
    {
        ~Restore()
        {
            auto& s = Render::SceneStreamServer::instance();
            s.setTrustProxy(false);
            s.setConnectionCaps(64, 16);
        }
    } restore;

    // Every connection arrives on loopback through the "proxy" and is
    // judged by what it forwarded: an address, or an identity.
    auto from = [&](const char* addr, const char* email = nullptr) {
        WsClient::Headers h {{"X-Forwarded-For", addr}};
        if (email) {
            h.emplace_back("X-Forwarded-Email", email);
        }
        return std::make_unique<WsClient>(port, "/scene", h);
    };
    auto served = [&](WsClient& c, const char* label) {
        c.hello(label);
        WsClient::Msg m = c.readBinary();
        return m.ok && m.data == asString(versioned(version, payload));
    };
    // The refusal rides the kick path, like a bad token: the reason,
    // the farewell, the close -- and it comes at the upgrade, before
    // any hello, since the address alone decided it.
    auto refused = [&](WsClient& c) {
        WsClient::Msg m = c.read();
        if (!m.ok || m.data != "{\"cmd\":\"error\",\"code\":\"TooMany\"}") {
            ADD_FAILURE() << "not refused: " << (m.ok ? m.data : m.ec.message());
            return false;
        }
        m = c.read();
        if (!m.ok || m.data != "{\"cmd\":\"error\",\"code\":\"Kicked\"}") {
            ADD_FAILURE() << "no farewell: " << (m.ok ? m.data : m.ec.message());
            return false;
        }
        m = c.read();
        return !m.ok && m.ec != net::error::timed_out;
    };

    // User A, by address: two connections; a third is one too many.
    auto a1 = from("203.0.113.1");
    EXPECT_TRUE(served(*a1, "cap-a1"));
    auto a2 = from("203.0.113.1");
    EXPECT_TRUE(served(*a2, "cap-a2"));
    auto a3 = from("203.0.113.1");
    EXPECT_TRUE(refused(*a3)) << "a third connection of one user";
    // User B, another address, has a count of its own.
    auto b1 = from("203.0.113.2");
    EXPECT_TRUE(served(*b1, "cap-b1"));
    // User C, by identity: one person from two addresses is one user,
    // with one user's cap.
    auto c1 = from("203.0.113.3", "c@example.test");
    EXPECT_TRUE(served(*c1, "cap-c1"));
    auto c2 = from("203.0.113.4", "c@example.test");
    EXPECT_TRUE(served(*c2, "cap-c2"));
    auto c3 = from("203.0.113.5", "c@example.test");
    EXPECT_TRUE(refused(*c3)) << "a third connection of one identity";
    // Three users are in; a fourth is one too many, wherever from.
    auto d1 = from("203.0.113.6");
    EXPECT_TRUE(refused(*d1)) << "a fourth user";

    // User A leaves entirely, and the seat is free again.
    a1.reset();
    a2.reset();
    a3.reset();
    Render::SceneClientInfo info;
    ASSERT_TRUE(waitFor([&] {
        return !findClient("cap-a1", info) && !findClient("cap-a2", info);
    }));
    auto d2 = from("203.0.113.6");
    EXPECT_TRUE(served(*d2, "cap-d2")) << "the seat a departed user held";

    // The anonymous legacy door on loopback -- what this whole suite
    // uses -- is counted nowhere: with three users in, it still gets in.
    WsClient local(port, "/scene");
    EXPECT_TRUE(served(local, "cap-local"));
}

TEST_F(SceneServerWire, stopClosesEveryConnectionAndARestartServesAgain)
{
    auto& server = Render::SceneStreamServer::instance();
    WsClient c(port, "/scene");
    c.hello("wire-stop");
    WsClient::Msg m = c.readBinary();
    ASSERT_TRUE(m.ok) << m.ec.message();

    server.stop();
    EXPECT_FALSE(server.running());
    m = c.read();
    ASSERT_TRUE(m.ok) << m.ec.message();
    EXPECT_EQ(m.data, "{\"cmd\":\"error\",\"code\":\"Kicked\"}");
    m = c.read();
    EXPECT_FALSE(m.ok);
    EXPECT_NE(m.ec, net::error::timed_out);
    Render::SceneClientInfo info;
    EXPECT_TRUE(waitFor([&] { return !findClient("wire-stop", info); }));

    ASSERT_TRUE(server.start(port)) << "a stopped server binds anew";
    EXPECT_TRUE(server.running());
    HttpReply r = httpRequest(port, http::verb::get, "/scene?v=0");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.status, 200u);
    EXPECT_EQ(r.body, asString(versioned(version, payload)))
        << "served groups keep their state across a stop";
    server.stop();
}
