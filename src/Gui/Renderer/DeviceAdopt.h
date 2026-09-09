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

#ifndef RENDERER_DEVICEADOPT_H
#define RENDERER_DEVICEADOPT_H

#include "Renderer.h"

class QWidget;

namespace Render
{

/// A graphics device this process did not create, offered to a backend
/// that would otherwise create one of its own.
///
/// This is Route D of docs/DeviceAdoption.md: Qt owns the device, bgfx
/// adopts it, and because there is then only ONE device, the frame bgfx
/// draws can be handed to Qt as a texture rather than copied through
/// system memory. What the readback composite pays per frame
/// (BGFXView::blitReadback) this pays once.
///
/// The struct is deliberately dumb and deliberately Qt-free: it is the
/// seam that keeps Qt's private RHI headers inside a single translation
/// unit (QtRhiDevice.cpp). Everything else -- the renderer factory, the
/// bgfx backend, the host that starts them -- sees only these fields.
struct AdoptedDevice
{
    enum Api {
        None,
        Metal,
        Vulkan,
        D3D11,
        D3D12,
    };

    Api api = None;

    /// The device itself: MTLDevice*, VkDevice, ID3D11Device* or
    /// ID3D12Device*. This is the field every backend reads
    /// (bgfx PlatformData::context).
    void *device = nullptr;

    /// The command queue, where one is part of the bargain.
    ///
    /// It is NOT uniform across backends and pretending otherwise is
    /// how this goes wrong: bgfx uses PlatformData::queue on D3D12
    /// only. Its Metal backend calls newCommandQueue() unconditionally,
    /// so a Metal session always has two queues and must synchronize
    /// with an MTLEvent or a completion handler; its Vulkan backend
    /// fetches queue 0 of the family, which MAY be the same VkQueue Qt
    /// uses -- compare the handles at runtime and take a host lock if
    /// they match, semaphores if they do not.
    void *queue = nullptr;

    /// Vulkan needs more than a device to be adopted usefully: the
    /// instance it belongs to, the physical device behind it, and the
    /// family the queue above came from. Null / zero elsewhere.
    void *instance = nullptr;
    void *physicalDevice = nullptr;
    unsigned queueFamily = 0;
    unsigned queueIndex = 0;

    bool valid() const { return api != None && device != nullptr; }

    /// The name the API goes by in a log line.
    const char *apiName() const
    {
        switch (api) {
        case Metal:  return "Metal";
        case Vulkan: return "Vulkan";
        case D3D11:  return "Direct3D11";
        case D3D12:  return "Direct3D12";
        default:     return "none";
        }
    }
};

/// Qt's own graphics device, borrowed.
///
/// Route D's ordering constraint is that bgfx::init happens once per
/// process, so the device must exist before the FIRST 3D view -- which
/// is exactly what the startup warm-up already arranges for the GL path
/// (Application.cpp, MainWindow's hidden GLSurfaceWarmup). These two
/// functions are the RHI analogue of that widget: create the surface
/// early, ask it for the device, hand the device to the backend.
///
/// Everything behind them is Qt private API (QRhi, QRhiTexture,
/// QRhiMetalNativeHandles), which lives under the versioned private
/// include path and carries no source or binary compatibility
/// guarantee. That dependency is real, it was taken deliberately, and
/// it is confined to QtRhiDevice.cpp so that a Qt release can only
/// break one file. See docs/DeviceAdoption.md, "The price".
namespace QtRhi
{

/// Is this build able to talk to Qt's RHI at all? False when the tree
/// was configured without the private Qt modules, in which case every
/// function below is a no-op and the host falls back to the GL warm-up.
RendererExport bool available();

/// Has the session asked for Route D? FC_RENDER_RHI=1.
///
/// Opt-in, and it will stay opt-in for a while: bringing a QRhiWidget
/// into the main window makes Qt build a QRhi for that window whether or
/// not anything else uses it, and the private-API dependency behind it
/// is the deliberate cost recorded in docs/DeviceAdoption.md. A session
/// that has not asked pays neither.
RendererExport bool warmupEnabled();

/// Which API a QRhi warm-up surface would come up on here, as an
/// AdoptedDevice::Api. None when RHI is unavailable or unconfigured.
/// Cheap: it reads the platform default and the FC_RENDER_RHI_API
/// override, and creates nothing.
RendererExport AdoptedDevice::Api preferredApi();

/// Create the hidden warm-up surface, a child of \a parent, that brings
/// Qt's QRhi up for that window. 1x1 and hidden, kept for the window's
/// lifetime, exactly like GLSurfaceWarmup and for exactly the same
/// reason: a device destroyed with the widget takes the session's one
/// bgfx::init with it.
///
/// Returns null when RHI is unavailable. The widget is owned by
/// \a parent.
RendererExport QWidget *createWarmupSurface(QWidget *parent);

/// The device \a surface brought up, or a null AdoptedDevice if it has
/// not initialised yet. A QRhiWidget builds its QRhi lazily, on the
/// first render, so a caller that needs the device NOW must say so --
/// which is what \a force does: it drives one frame of the hidden
/// surface to make the device exist.
RendererExport bool warmupDevice(QWidget *surface, AdoptedDevice &out,
                                 bool force = true);

}  // namespace QtRhi

}  // namespace Render

#endif  // RENDERER_DEVICEADOPT_H
