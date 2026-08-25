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

#include "DrawDevice.h"
#include "DrawSurface.h"
#include "Renderer.h"

// The backend-free half of the draw facade: resolving the active
// backend's device. The backend half is BGFXDrawDevice.cpp (built only
// with the bgfx backend); without one, instance() is null and the
// consumers keep their fallback paths.

namespace Render {

DrawDevice::~DrawDevice()
{}

DrawSurface::~DrawSurface()
{}

DrawDevice *DrawDevice::instance()
{
    return RendererFactory::drawDevice();
}

std::unique_ptr<DrawSurface> DrawSurface::create(QOpenGLWidget *widget,
                                                 unsigned numPasses)
{
    DrawDevice *device = DrawDevice::instance();
    if (!device)
        return nullptr;
    return device->createSurface(widget, numPasses);
}

} // namespace Render
