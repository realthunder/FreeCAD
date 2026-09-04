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
 *                                                                          *
 ****************************************************************************/

#include "EngineGraphHost.h"
#include "GraphHost.h"
#include "ImGuiBgfx.h"

#include "../DrawDevice.h"
#include "../ImageDecode.h"
#include "../MaterialXSupportP.h"

#include <MaterialXFormat/XmlIo.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

namespace Render::GraphEditor {

namespace {

struct Thumbnail {
    TextureHandle texture;
    int width = 0;
    int height = 0;
    /// Resolved or decoded and found wanting: not asked again until
    /// the set is invalidated.
    bool failed = false;
};

void destroyTexture(TextureHandle &texture)
{
    if (!texture.valid())
        return;
    if (DrawDevice *device = DrawDevice::instance())
        device->destroy(texture);
    texture = {};
}

} // namespace

/// The GraphHost the editor sees: every answer comes from the owning
/// EngineGraphHost's state, every question that needs the platform
/// goes to its virtuals.
class EngineGraphHostAdapter : public GraphHost {
public:
    explicit EngineGraphHostAdapter(EngineGraphHost &owner)
        : owner(owner)
    {}

    void documentChanged(const ::MaterialX::DocumentPtr &doc,
                         ::MaterialX::ElementPtr) override
    {
        textChanged(doc);
    }
    void valueDragged(const ::MaterialX::DocumentPtr &doc, ::MaterialX::InputPtr,
                      ::MaterialX::ValuePtr) override
    {
        // The document already carries the dragged value: the preview
        // is the whole answer, the property write waits for release.
        textChanged(doc);
    }
    bool hasImplementation(const ::MaterialX::NodeDef &def) override
    {
        return Render::MaterialX::hasImplementation(def);
    }
    ImTextureID thumbnail(const std::string &name, int &w, int &h) override;
    const std::vector<std::string> &imageExtensions() override
    {
        static const std::vector<std::string> extensions = {
            "png", "jpg", "jpeg", "bmp", "tga", "tif", "tiff", "exr", "hdr"};
        return extensions;
    }
    const std::vector<std::string> &imageNames() override;
    ImTextureID preview(int w, int h) override;
    void previewMouse(float x, float y, int button, bool down) override;
    void previewScroll(float delta) override;
    bool compiling() const override;
    const std::vector<std::string> &surfaceNames() override { return owner.surfaceNames(); }
    std::string currentSurface() override { return owner.currentSurface(); }
    void selectSurface(const std::string &name) override { owner.selectSurface(name); }
    const std::vector<std::string> &previewModes() override { return owner.previewModes(); }
    int previewMode() override { return owner.previewMode(); }
    void setPreviewMode(int mode) override { owner.setPreviewMode(mode); }
    const std::vector<std::string> &previewDevices() override { return owner.previewDevices(); }
    int previewDevice() override { return owner.previewDevice(); }
    void setPreviewDevice(int device) override { owner.setPreviewDevice(device); }
    std::string previewStatus() override { return owner.previewStatus(); }

private:
    void textChanged(const ::MaterialX::DocumentPtr &doc);

    EngineGraphHost &owner;
};

struct EngineGraphHost::Private {
    Private(EngineGraphHost &owner)
        : adapter(owner)
    {}

    EngineGraphHostAdapter adapter;
    std::string text;
    std::map<std::string, Thumbnail> thumbnails;
    std::vector<std::string> names;
    TextureHandle preview;
    int previewW = 0;
    int previewH = 0;
    int wantW = 0;
    int wantH = 0;
    Camera camera;
    bool compiling = false;
    std::string status;
    /// The preview pane's drag: which button holds it and where the
    /// pointer last was.
    int dragButton = -1;
    float lastX = 0.0f;
    float lastY = 0.0f;
};

// ---- the adapter

void EngineGraphHostAdapter::textChanged(const ::MaterialX::DocumentPtr &doc)
{
    if (!doc)
        return;
    std::string text = ::MaterialX::writeToXmlString(doc);
    if (text == owner.d->text)
        return;
    owner.d->text = std::move(text);
    owner.previewInvalidated();
}

ImTextureID EngineGraphHostAdapter::thumbnail(const std::string &name, int &w, int &h)
{
    w = h = 0;
    auto &slot = owner.d->thumbnails[name];
    if (slot.texture.valid()) {
        w = slot.width;
        h = slot.height;
        return ImTextureID(ImGuiBgfx::packTexture(slot.texture.idx));
    }
    if (slot.failed)
        return ImTextureID_Invalid;
    DrawDevice *device = DrawDevice::instance();
    if (!device)
        return ImTextureID_Invalid;   // asked again when it is up
    const std::string path = owner.resolveImage(name);
    std::vector<uint8_t> rgba;
    int width = 0, height = 0;
    if (path.empty() || !owner.loadImage(path, width, height, rgba)
            || width <= 0 || height <= 0
            || rgba.size() < size_t(width) * size_t(height) * 4) {
        slot.failed = true;
        return ImTextureID_Invalid;
    }
    slot.texture = device->createTexture2D(width, height, DrawTextureFormat::RGBA8,
                                           TextureClamp, rgba.data());
    if (!slot.texture.valid()) {
        slot.failed = true;
        return ImTextureID_Invalid;
    }
    slot.width = w = width;
    slot.height = h = height;
    return ImTextureID(ImGuiBgfx::packTexture(slot.texture.idx));
}

const std::vector<std::string> &EngineGraphHostAdapter::imageNames()
{
    owner.d->names = owner.imageNames();
    return owner.d->names;
}

ImTextureID EngineGraphHostAdapter::preview(int w, int h)
{
    if (w > 0 && h > 0 && (w != owner.d->wantW || h != owner.d->wantH)) {
        owner.d->wantW = w;
        owner.d->wantH = h;
        owner.previewInvalidated();
    }
    if (!owner.d->preview.valid())
        return ImTextureID_Invalid;
    return ImTextureID(ImGuiBgfx::packTexture(owner.d->preview.idx));
}

void EngineGraphHostAdapter::previewMouse(float x, float y, int button, bool down)
{
    auto &d = *owner.d;
    if (button >= 0) {
        if (down) {
            d.dragButton = button;
            d.lastX = x;
            d.lastY = y;
        }
        else if (d.dragButton == button) {
            d.dragButton = -1;
        }
        return;
    }
    // A motion report while a button is held: orbit. Screen y grows
    // downward, so dragging up raises the eye.
    if (d.dragButton < 0)
        return;
    const float dx = x - d.lastX;
    const float dy = y - d.lastY;
    d.lastX = x;
    d.lastY = y;
    if (dx == 0.0f && dy == 0.0f)
        return;
    const float rate = 0.01f;
    d.camera.yaw -= dx * rate;
    d.camera.pitch = std::clamp(d.camera.pitch + dy * rate, -1.5f, 1.5f);
    owner.previewInvalidated();
}

void EngineGraphHostAdapter::previewScroll(float delta)
{
    auto &d = *owner.d;
    const float factor = std::pow(0.9f, delta);
    d.camera.distance = std::clamp(d.camera.distance * factor, 1.3f, 20.0f);
    owner.previewInvalidated();
}

bool EngineGraphHostAdapter::compiling() const
{
    return owner.d->compiling;
}

// ---- the host

EngineGraphHost::EngineGraphHost()
    : d(new Private(*this))
{}

EngineGraphHost::~EngineGraphHost()
{
    invalidateThumbnails();
    destroyTexture(d->preview);
}

GraphHost *EngineGraphHost::graphHost() const
{
    return &d->adapter;
}

std::string EngineGraphHost::resolveImage(const std::string &name)
{
    if (name.empty())
        return {};
    std::ifstream in(name, std::ios::in | std::ios::binary);
    return in ? name : std::string();
}

bool EngineGraphHost::loadImage(const std::string &path, int &width, int &height,
                                std::vector<uint8_t> &rgba)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
        return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty() || !isEncodedImage(bytes.data(), bytes.size()))
        return false;
    std::vector<uint8_t> rows;
    if (!decodeImage(bytes.data(), bytes.size(), 4, kThumbnailSide, width, height, rows))
        return false;
    // The decoder hands rows bottom-up like GL; a thumbnail is drawn
    // top-down.
    const size_t stride = size_t(width) * 4;
    rgba.resize(rows.size());
    for (int y = 0; y < height; ++y)
        std::copy_n(rows.data() + size_t(height - 1 - y) * stride, stride,
                    rgba.data() + size_t(y) * stride);
    return true;
}

const std::string &EngineGraphHost::documentText() const
{
    return d->text;
}

int EngineGraphHost::previewWidth() const
{
    return d->wantW;
}

int EngineGraphHost::previewHeight() const
{
    return d->wantH;
}

const EngineGraphHost::Camera &EngineGraphHost::camera() const
{
    return d->camera;
}

void EngineGraphHost::cameraFrame(float eye[3], float forward[3], float up[3]) const
{
    const Camera &c = d->camera;
    const float cp = std::cos(c.pitch);
    // yaw 0 puts the eye on -Y looking at +Y: the viewport's front
    // view. Z is up.
    eye[0] = c.distance * cp * std::sin(c.yaw);
    eye[1] = -c.distance * cp * std::cos(c.yaw);
    eye[2] = c.distance * std::sin(c.pitch);
    const float len = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    forward[0] = -eye[0] / len;
    forward[1] = -eye[1] / len;
    forward[2] = -eye[2] / len;
    // World Z with its forward component removed; at the poles the
    // horizontal direction the yaw states stands in.
    float u[3] = {0.0f, 0.0f, 1.0f};
    const float dot = forward[2];
    u[0] -= forward[0] * dot;
    u[1] -= forward[1] * dot;
    u[2] -= forward[2] * dot;
    float ulen = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ulen < 1e-4f) {
        u[0] = std::sin(c.yaw);
        u[1] = -std::cos(c.yaw);
        u[2] = 0.0f;
        if (c.pitch > 0.0f) {
            u[0] = -u[0];
            u[1] = -u[1];
        }
        ulen = 1.0f;
    }
    up[0] = u[0] / ulen;
    up[1] = u[1] / ulen;
    up[2] = u[2] / ulen;
}

void EngineGraphHost::setPreviewImage(int width, int height, const uint8_t *rgba)
{
    if (width <= 0 || height <= 0 || !rgba) {
        clearPreview();
        return;
    }
    DrawDevice *device = DrawDevice::instance();
    if (!device) {
        clearPreview();
        return;
    }
    const uint32_t bytes = uint32_t(width) * uint32_t(height) * 4;
    if (d->preview.valid() && (width != d->previewW || height != d->previewH))
        destroyTexture(d->preview);
    if (d->preview.valid()) {
        device->updateTexture2D(d->preview, 0, 0, width, height, rgba, bytes);
        return;
    }
    // Created EMPTY and then written: a bgfx texture created with its
    // data is immutable, and every later update would be dropped
    // without a word -- the first preview would be the only one.
    d->preview = device->createTexture2D(width, height, DrawTextureFormat::RGBA8,
                                         TextureClamp, nullptr);
    if (!d->preview.valid())
        return;
    d->previewW = width;
    d->previewH = height;
    device->updateTexture2D(d->preview, 0, 0, width, height, rgba, bytes);
}

void EngineGraphHost::clearPreview()
{
    destroyTexture(d->preview);
    d->previewW = d->previewH = 0;
}

void EngineGraphHost::setCompiling(bool compiling)
{
    d->compiling = compiling;
}

bool EngineGraphHost::compiling() const
{
    return d->compiling;
}

void EngineGraphHost::setPreviewStatus(const std::string &status)
{
    d->status = status;
}

const std::string &EngineGraphHost::previewStatus() const
{
    return d->status;
}

void EngineGraphHost::invalidateThumbnails()
{
    for (auto &entry : d->thumbnails)
        destroyTexture(entry.second.texture);
    d->thumbnails.clear();
}

} // namespace Render::GraphEditor
