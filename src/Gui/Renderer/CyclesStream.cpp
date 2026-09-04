/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

// The served viewport (docs/CyclesIntegration.md sec 7.1): a Viewport
// whose frames are encoded and handed to a remote viewer instead of
// blitted. Only built with the engine (HAVE_CYCLES); the no-engine
// stub of FrameStream::create lives in CyclesRenderer.cpp.

#include "CyclesRenderer.h"
#include "CyclesSceneP.h"
#include "FrameStreamWire.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/half.h"

namespace Render::Cycles {

namespace {

float srgbToLinear(float v)
{
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

unsigned char encode(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    const float e = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<unsigned char>(std::lround(e * 255.0f));
}

unsigned char quantize(float v)
{
    return static_cast<unsigned char>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

/// 0xRRGGBBAA authored colour, decoded to linear when managed.
void unpackAuthored(uint32_t rgba, float out[3], bool managed)
{
    out[0] = ((rgba >> 24) & 0xff) / 255.0f;
    out[1] = ((rgba >> 16) & 0xff) / 255.0f;
    out[2] = ((rgba >> 8) & 0xff) / 255.0f;
    if (managed)
        for (int i = 0; i < 3; ++i)
            out[i] = srgbToLinear(out[i]);
}

}  // namespace

// Bottom-up premultiplied linear half4 to 8-bit rows, composited over
// the background where the film was transparent: the same flat colour
// or vertical ramp the offline render writes (CyclesRenderer.cpp
// writePng), encoded exactly when the scene is colour managed. The
// stream takes it top-down RGB for the JPEG encoder; the shader graph
// editor's preview bottom-up RGBA, as GL reads a framebuffer back.
void compositeFrame(const void *half4, int width, int height,
                    const Background &background, bool managed,
                    int channels, bool topDown, std::vector<uint8_t> &out)
{
    const ccl::half4 *px = static_cast<const ccl::half4 *>(half4);
    if (!px || width <= 0 || height <= 0 || (channels != 3 && channels != 4)) {
        out.clear();
        return;
    }
    float from[3], to[3], mid[3];
    unpackAuthored(background.fromColor, from, managed);
    unpackAuthored(background.toColor, to, managed);
    unpackAuthored(background.midColor, mid, managed);
    out.resize(size_t(width) * size_t(height) * size_t(channels));
    for (int y = 0; y < height; ++y) {
        // The ramp runs top to bottom in the row order of the OUTPUT;
        // y here is the output row.
        const float t = height > 1
            ? float(topDown ? y : height - 1 - y) / float(height - 1) : 0.0f;
        float bg[3];
        for (int i = 0; i < 3; ++i) {
            if (background.type == Background::Flat)
                bg[i] = from[i];
            else if (background.hasMid)
                bg[i] = t < 0.5f ? from[i] + (mid[i] - from[i]) * (t * 2.0f)
                                 : mid[i] + (to[i] - mid[i]) * ((t - 0.5f) * 2.0f);
            else
                bg[i] = from[i] + (to[i] - from[i]) * t;
        }
        const int srcRow = topDown ? height - 1 - y : y;
        const ccl::half4 *src = px + size_t(srcRow) * size_t(width);
        uint8_t *dst = out.data() + size_t(y) * size_t(width) * size_t(channels);
        for (int x = 0; x < width; ++x, ++src, dst += channels) {
            const float a = std::clamp(ccl::half_to_float(src->w), 0.0f, 1.0f);
            const float c[3] = {ccl::half_to_float(src->x), ccl::half_to_float(src->y),
                                ccl::half_to_float(src->z)};
            for (int i = 0; i < 3; ++i) {
                const float v = c[i] + bg[i] * (1.0f - a);
                dst[i] = managed ? encode(v) : quantize(v);
            }
            if (channels == 4)
                dst[3] = 255;
        }
    }
}

namespace {

class FrameStreamImpl : public FrameStream
{
public:
    FrameStreamImpl(const StreamOptions &options,
                    std::function<bool(std::vector<uint8_t> &&)> send,
                    std::function<void(const std::string &)> notify)
        : options(options)
        , send(std::move(send))
        , notify(std::move(notify))
    {}

    bool start(std::string *error)
    {
        viewport = Viewport::create(options.viewport, error);
        if (!viewport)
            return false;
        // Cycles' threads report a staged frame; the encoder thread
        // is what takes it.
        viewport->setRedrawCallback([this] {
            {
                std::lock_guard<std::mutex> lock(wakeMutex);
                pending = true;
            }
            wake.notify_one();
        });
        worker = std::thread([this] { run(); });
        return true;
    }

    ~FrameStreamImpl() override
    {
        {
            std::lock_guard<std::mutex> lock(wakeMutex);
            quit = true;
        }
        wake.notify_all();
        if (worker.joinable())
            worker.join();
        // The callback first, so a render thread mid-update finds
        // nothing to wake; then the session goes with the viewport.
        std::lock_guard<std::mutex> lock(vpMutex);
        if (viewport)
            viewport->setRedrawCallback(nullptr);
        viewport.reset();
    }

    void setScene(const SceneInput &input) override
    {
        std::lock_guard<std::mutex> lock(vpMutex);
        if (!viewport)
            return;
        managed = input.output.transform == OutputConfig::SRGB;
        background = input.background;
        if (haveCamera) {
            SceneInput in = input;
            in.camera = camera;
            viewport->setScene(in);
        }
        else {
            camera = capped(input.camera);
            SceneInput in = input;
            in.camera = camera;
            viewport->setScene(in);
        }
        sceneSet = true;
    }

    void setCamera(const CameraInput &input) override
    {
        CameraInput c = capped(input);
        std::lock_guard<std::mutex> lock(vpMutex);
        camera = c;
        haveCamera = true;
        if (viewport && sceneSet)
            viewport->setCamera(camera);
    }

    ViewportStatus status() const override
    {
        std::lock_guard<std::mutex> lock(vpMutex);
        return viewport ? viewport->status() : ViewportStatus();
    }

    bool lost() const override
    {
        return lostFlag.load();
    }

private:
    /// The viewer's size capped to maxPixels, aspect kept; the
    /// projection is aspect-only, so it stays valid.
    CameraInput capped(const CameraInput &in) const
    {
        CameraInput c = in;
        c.width = std::max(c.width, 1);
        c.height = std::max(c.height, 1);
        const long pixels = long(c.width) * long(c.height);
        if (options.maxPixels > 0 && pixels > options.maxPixels) {
            const double scale = std::sqrt(double(options.maxPixels) / double(pixels));
            c.width = std::max(1, int(std::floor(c.width * scale)));
            c.height = std::max(1, int(std::floor(c.height * scale)));
        }
        return c;
    }

    void run()
    {
        using clock = std::chrono::steady_clock;
        const auto interval = std::chrono::milliseconds(
                std::max(options.minIntervalMs, 0));
        clock::time_point lastSend{};
        std::vector<uint8_t> rgb;
        bool errorReported = false;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(wakeMutex);
                wake.wait(lock, [this] { return pending || quit; });
                if (quit)
                    return;
                pending = false;
                // Pace: no sooner than the interval after the last
                // send. A newer frame that lands meanwhile is what
                // gets taken -- nothing queues.
                const auto due = lastSend + interval;
                if (clock::now() < due) {
                    wake.wait_until(lock, due, [this] { return quit; });
                    if (quit)
                        return;
                    pending = false;
                }
            }

            int width = 0;
            int height = 0;
            bool got = false;
            ViewportStatus st;
            bool enc;
            {
                std::lock_guard<std::mutex> lock(vpMutex);
                if (!viewport)
                    return;
                enc = managed;
                got = viewport->takeFrame([&](const void *px, int w, int h) {
                    compositeFrame(px, w, h, background, managed, 3, true, rgb);
                    width = w;
                    height = h;
                });
                st = viewport->status();
            }
            if (!st.error.empty() && !errorReported) {
                errorReported = true;
                lostFlag = true;
                if (notify) {
                    std::string msg = "{\"op\":\"cycles\",\"event\":\"error\",\"cell\":"
                        + std::to_string(options.cell) + ",\"message\":\"";
                    for (char ch : st.error) {
                        // Any control character breaks the JSON string
                        // (a newline is just the one an engine message
                        // actually carries); a space keeps it readable.
                        if (ch == '"' || ch == '\\') {
                            msg += '\\';
                            msg += ch;
                        }
                        else if (static_cast<unsigned char>(ch) < 0x20)
                            msg += ' ';
                        else
                            msg += ch;
                    }
                    msg += "\"}";
                    notify(msg);
                }
            }
            if (!got || width <= 0 || height <= 0)
                continue;

            QImage image(rgb.data(), width, height, width * 3, QImage::Format_RGB888);
            QByteArray jpeg;
            QBuffer buffer(&jpeg);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "JPEG", std::clamp(options.quality, 1, 100));
            buffer.close();
            if (jpeg.isEmpty())
                continue;

            StreamedFrameHeader header;
            header.cell = uint8_t(std::clamp(options.cell, 0, 255));
            header.width = uint32_t(width);
            header.height = uint32_t(height);
            header.sequence = ++sequence;
            header.progress = st.progress;
            header.status = st.status;
            if (st.progress >= 0.999f)
                header.flags |= StreamedFrameHeader::Final;
            if (enc)
                header.flags |= StreamedFrameHeader::Encoded;
            std::vector<uint8_t> msg;
            msg.reserve(64 + size_t(jpeg.size()));
            writeStreamedFrameHeader(msg, header);
            msg.insert(msg.end(), jpeg.begin(), jpeg.end());
            lastSend = clock::now();
            if (!send || !send(std::move(msg))) {
                // The viewer is gone: nothing to encode for any more.
                lostFlag = true;
                return;
            }
        }
    }

    StreamOptions options;
    std::function<bool(std::vector<uint8_t> &&)> send;
    std::function<void(const std::string &)> notify;

    mutable std::mutex vpMutex;
    std::unique_ptr<Viewport> viewport;
    CameraInput camera;
    bool haveCamera = false;
    bool sceneSet = false;
    bool managed = true;
    Background background;

    std::mutex wakeMutex;
    std::condition_variable wake;
    bool pending = false;
    bool quit = false;
    std::thread worker;
    uint32_t sequence = 0;
    std::atomic<bool> lostFlag{false};
};

}  // namespace

std::unique_ptr<FrameStream> FrameStream::create(
        const StreamOptions &options,
        std::function<bool(std::vector<uint8_t> &&)> send,
        std::function<void(const std::string &)> notify,
        std::string *error)
{
    std::unique_ptr<FrameStreamImpl> stream;
    {
        // The cap (sec 7.1) is read with the count frozen: the
        // construction that takes the slot happens under the same
        // lock, so two connections cannot both find room for the last
        // one. The device, which is what takes the time, is created
        // outside it by start().
        static std::mutex admit;
        std::lock_guard<std::mutex> lock(admit);
        if (options.maxStreams > 0 && liveCount() >= options.maxStreams) {
            if (error) {
                *error = "this server already runs " + std::to_string(liveCount())
                    + " path-traced sessions, which is its limit ("
                    + std::to_string(options.maxStreams) + ")";
            }
            return nullptr;
        }
        stream =
            std::make_unique<FrameStreamImpl>(options, std::move(send), std::move(notify));
    }
    if (!stream->start(error))
        return nullptr;
    return stream;
}

}  // namespace Render::Cycles
