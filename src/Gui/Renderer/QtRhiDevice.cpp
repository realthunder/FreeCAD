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

/// THE Qt-private translation unit. Nothing else in this tree includes
/// <rhi/qrhi.h>, and that is the whole design: QRhi, QRhiTexture and the
/// per-backend native-handle structs live under Qt's versioned private
/// include path and carry no source or binary compatibility guarantee,
/// so the blast radius of a Qt release is one file with a reviewable
/// surface -- nativeHandles(), the device/queue fields, create() and
/// backend(). QRhiWidget itself is public QtWidgets and forward-declares
/// every RHI type it returns, so subclassing it costs nothing extra.
///
/// See docs/DeviceAdoption.md, "The price: a Qt private-API dependency".

#include "DeviceAdopt.h"

#ifdef FC_RENDERER_QT_RHI

#include <QImage>
#include <QRhiWidget>
#include <QString>
#include <QWidget>

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <Base/Console.h>

#endif  // FC_RENDERER_QT_RHI

namespace Render
{
namespace QtRhi
{

#ifndef FC_RENDERER_QT_RHI

// Configured without the private Qt modules. Every entry point answers
// "no", and the host falls back to the GL warm-up it has always used.
bool available() { return false; }
bool warmupEnabled() { return false; }
AdoptedDevice::Api preferredApi() { return AdoptedDevice::None; }
QWidget *createWarmupSurface(QWidget *) { return nullptr; }
bool warmupDevice(QWidget *, AdoptedDevice &out, bool)
{
    out = AdoptedDevice();
    return false;
}

#else

namespace
{

/// FC_RENDER_RHI_API: name the backend Qt's RHI should come up on,
/// overriding the platform default. "metal", "vulkan", "d3d11",
/// "d3d12". Anything else leaves the default alone.
AdoptedDevice::Api apiOverride()
{
    const char *v = getenv("FC_RENDER_RHI_API");
    if (!v || !*v)
        return AdoptedDevice::None;
    const QString s = QString::fromLatin1(v).toLower();
    if (s == QLatin1String("metal"))
        return AdoptedDevice::Metal;
    if (s == QLatin1String("vulkan"))
        return AdoptedDevice::Vulkan;
    if (s == QLatin1String("d3d11") || s == QLatin1String("direct3d11"))
        return AdoptedDevice::D3D11;
    if (s == QLatin1String("d3d12") || s == QLatin1String("direct3d12"))
        return AdoptedDevice::D3D12;
    Base::Console().Warning(
            "FC_RENDER_RHI_API=%s is not a backend name; ignoring\n", v);
    return AdoptedDevice::None;
}

QRhiWidget::Api toWidgetApi(AdoptedDevice::Api api)
{
    switch (api) {
    case AdoptedDevice::Metal:  return QRhiWidget::Api::Metal;
    case AdoptedDevice::Vulkan: return QRhiWidget::Api::Vulkan;
    case AdoptedDevice::D3D11:  return QRhiWidget::Api::Direct3D11;
    case AdoptedDevice::D3D12:  return QRhiWidget::Api::Direct3D12;
    default:                    return QRhiWidget::Api::Null;
    }
}

QRhi::Implementation toRhiImpl(AdoptedDevice::Api api)
{
    switch (api) {
    case AdoptedDevice::Metal:  return QRhi::Metal;
    case AdoptedDevice::Vulkan: return QRhi::Vulkan;
    case AdoptedDevice::D3D11:  return QRhi::D3D11;
    case AdoptedDevice::D3D12:  return QRhi::D3D12;
    default:                    return QRhi::Null;
    }
}

/// The one place a live QRhi is turned into the Qt-free struct the rest
/// of the tree passes around. Everything private this route needs is
/// read here and nowhere else.
///
/// ! The queue is NOT uniform, and the AdoptedDevice comment says why:
/// bgfx reads PlatformData::queue on D3D12 only, its Metal backend
/// always makes a second queue of its own, and its Vulkan backend takes
/// queue 0 of the family -- which may or may not be the very VkQueue
/// recorded here. Carrying the handle regardless is what lets the sync
/// abstraction compare them at runtime instead of guessing.
bool readNativeHandles(QRhi *rhi, AdoptedDevice &out)
{
    out = AdoptedDevice();
    if (!rhi)
        return false;
    switch (rhi->backend()) {
#if QT_CONFIG(metal)
    case QRhi::Metal: {
        const auto *h = static_cast<const QRhiMetalNativeHandles *>(
                rhi->nativeHandles());
        if (!h)
            return false;
        out.api = AdoptedDevice::Metal;
        out.device = static_cast<void *>(h->dev);
        out.queue = static_cast<void *>(h->cmdQueue);
        break;
    }
#endif
// Qt's own guard on the declaration, mirrored EXACTLY rather than
// approximated. QT_CONFIG(vulkan) alone is not it: qrhi_platform.h
// declares QRhiVulkanNativeHandles under
// `QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)`, and the two
// halves come apart on a real box -- this conda Qt 6.11.2 on Windows
// reports QT_CONFIG(vulkan) true while shipping no vulkan/vulkan.h, so
// the struct does not exist and this branch failed to compile. A guard
// on a declaration has to be the declaration's guard; anything else is
// a guess that holds only where it was written.
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    case QRhi::Vulkan: {
        const auto *h = static_cast<const QRhiVulkanNativeHandles *>(
                rhi->nativeHandles());
        if (!h)
            return false;
        out.api = AdoptedDevice::Vulkan;
        out.device = reinterpret_cast<void *>(h->dev);
        out.queue = reinterpret_cast<void *>(h->gfxQueue);
        out.physicalDevice = reinterpret_cast<void *>(h->physDev);
        out.instance = static_cast<void *>(h->inst);
        out.queueFamily = unsigned(h->gfxQueueFamilyIdx);
        out.queueIndex = unsigned(h->gfxQueueIdx);
        break;
    }
#endif
#ifdef Q_OS_WIN
    case QRhi::D3D11: {
        const auto *h = static_cast<const QRhiD3D11NativeHandles *>(
                rhi->nativeHandles());
        if (!h)
            return false;
        out.api = AdoptedDevice::D3D11;
        out.device = h->dev;
        out.queue = h->context;
        break;
    }
    case QRhi::D3D12: {
        const auto *h = static_cast<const QRhiD3D12NativeHandles *>(
                rhi->nativeHandles());
        if (!h)
            return false;
        out.api = AdoptedDevice::D3D12;
        out.device = h->dev;
        out.queue = h->commandQueue;
        break;
    }
#endif
    default:
        // OpenGLES2 and Null. Neither is a device worth adopting: the
        // GL route already has one (and shares it through Qt's global
        // share group), and Null has nothing behind it.
        return false;
    }
    return out.valid();
}

/// The RHI analogue of MainWindow's hidden GLSurfaceWarmup: 1x1, never
/// shown, kept for the window's lifetime. It exists to make Qt build
/// its QRhi BEFORE the first 3D view, because bgfx::init happens once
/// per process and whoever gets there first decides the device for
/// every view afterwards (docs/DeviceAdoption.md section 4, constraint
/// 2).
///
/// It draws nothing. render() is deliberately empty -- there is no
/// frame here worth submitting, and the widget is never composited.
class WarmupSurface : public QRhiWidget
{
public:
    explicit WarmupSurface(QWidget *parent)
        : QRhiWidget(parent)
    {}

    AdoptedDevice device;
    /// Qt reached initialize() and built the QRhi above.
    ///
    /// ! This is NOT the same as "the device belongs to the window",
    /// and stage 4 needs the second thing. A top level has exactly one
    /// composition API; when it is already spoken for -- and it is,
    /// because MainWindow's GLSurfaceWarmup is a QOpenGLWidget created
    /// just before this surface -- Qt warns "already using another
    /// graphics API for composition" and builds this widget a QRhi of
    /// its own instead. Measured 2026-09-09, docs/DeviceAdoption.md
    /// section 12. So there are three states, not two: the window's,
    /// Qt's but widget-local, and ours below.
    bool fromQt = false;
    /// A QRhi we created because Qt's was not up. It is a device, and
    /// it is NOT the window's -- enough for stage 2 (bgfx has a device
    /// before the first view), not enough for stage 4 (a QRhiWidget
    /// viewport must share the window's).
    QRhi *ownRhi = nullptr;

    ~WarmupSurface() override
    {
        delete ownRhi;
    }

protected:
    void initialize(QRhiCommandBuffer *) override
    {
        if (fromQt)
            return;
        if (!readNativeHandles(rhi(), device))
            return;
        fromQt = true;
        Base::Console().Log(
                "Init: Qt RHI is up on %s; device %p, queue %p\n",
                rhi()->backendName(), device.device, device.queue);
    }

    void render(QRhiCommandBuffer *) override {}
};

}  // namespace

bool available() { return true; }

bool warmupEnabled()
{
    static const bool on = [] {
        const char *v = getenv("FC_RENDER_RHI");
        return v && *v && *v != '0';
    }();
    return on;
}

AdoptedDevice::Api preferredApi()
{
    const AdoptedDevice::Api forced = apiOverride();
    if (forced != AdoptedDevice::None)
        return forced;
#if defined(Q_OS_MACOS)
    return AdoptedDevice::Metal;
#elif defined(Q_OS_WIN)
    return AdoptedDevice::D3D12;
#else
    return AdoptedDevice::Vulkan;
#endif
}

QWidget *createWarmupSurface(QWidget *parent)
{
    const AdoptedDevice::Api api = preferredApi();
    const QRhiWidget::Api wapi = toWidgetApi(api);
    if (wapi == QRhiWidget::Api::Null) {
        Base::Console().Warning(
                "Init: no QRhi backend to warm up on this platform\n");
        return nullptr;
    }
    auto *w = new WarmupSurface(parent);
    w->setObjectName(QStringLiteral("RhiSurfaceWarmup"));
    w->setApi(wapi);
    w->resize(1, 1);
    w->hide();
    return w;
}

bool warmupDevice(QWidget *surface, AdoptedDevice &out, bool force)
{
    out = AdoptedDevice();
    // dynamic_cast, not qobject_cast: WarmupSurface carries no Q_OBJECT
    // of its own, so qobject_cast would test QRhiWidget's meta object
    // and happily "succeed" on any QRhiWidget at all.
    auto *w = dynamic_cast<WarmupSurface *>(surface);
    if (!w)
        return false;
    if (!w->fromQt && force) {
        // A QRhiWidget builds its QRhi lazily, on its first render, and
        // this one is hidden and never painted. grabFramebuffer() is
        // the public way to say "render now" -- it runs initialize()
        // and render() and hands back the image nobody here wants.
        (void)w->grabFramebuffer();
    }
    if (w->fromQt) {
        out = w->device;
        return out.valid();
    }
    // Qt's QRhi did not come up. That is expected before the main
    // window has a native window behind it -- a QRhiWidget draws
    // through the top level's backing store, and the warm-up runs under
    // the splash screen, deliberately early.
    //
    // A device of our own still satisfies the ORDERING constraint this
    // stage exists for: bgfx::init is once per process, and it must not
    // be the first 3D view that decides which device the session runs
    // on. It does NOT satisfy stage 4 -- a QRhiWidget viewport has to
    // share the window's QRhi to hand its texture over without a copy
    // -- so say which of the two this is rather than let a later stage
    // discover it.
    if (!w->ownRhi) {
        const AdoptedDevice::Api api = preferredApi();
        QRhiInitParams *params = nullptr;
#if QT_CONFIG(metal)
        QRhiMetalInitParams metalParams;
        if (api == AdoptedDevice::Metal)
            params = &metalParams;
#endif
#ifdef Q_OS_WIN
        QRhiD3D11InitParams d3d11Params;
        QRhiD3D12InitParams d3d12Params;
        if (api == AdoptedDevice::D3D11)
            params = &d3d11Params;
        else if (api == AdoptedDevice::D3D12)
            params = &d3d12Params;
#endif
        // Vulkan is deliberately not in that list. QRhiVulkanInitParams
        // wants a QVulkanInstance, and one made here would be a second
        // instance beside the one Qt creates for its own windows --
        // which is the opposite of what adopting a device is for. On
        // Vulkan this route waits for Qt's own QRhi rather than
        // manufacturing a rival.
        if (!params) {
            AdoptedDevice named;
            named.api = api;
            Base::Console().Warning(
                    "Init: Qt's QRhi is not up yet and no standalone"
                    " fallback exists for %s; the renderer will bring up"
                    " its own device\n", named.apiName());
            return false;
        }
        w->ownRhi = QRhi::create(toRhiImpl(api), params);
        if (!w->ownRhi) {
            Base::Console().Warning(
                    "Init: could not create a QRhi device to adopt\n");
            return false;
        }
        Base::Console().Warning(
                "Init: Qt's own QRhi was not up yet, so the renderer"
                " adopted a SEPARATE QRhi on %s. Ordering is satisfied;"
                " a QRhiWidget viewport would still need the window's"
                " device (docs/DeviceAdoption.md stage 4).\n",
                w->ownRhi->backendName());
    }
    return readNativeHandles(w->ownRhi, out);
}

#endif  // FC_RENDERER_QT_RHI

}  // namespace QtRhi
}  // namespace Render
