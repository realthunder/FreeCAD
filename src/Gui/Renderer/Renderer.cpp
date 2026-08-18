/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include "Renderer.h"

#include <chrono>
#include <map>
#include <vector>
#include <string>
#ifdef FC_RENDERER_STANDALONE
#include "StandalonePlatform.h"
#else
#include <QDebug>
#endif

using namespace Render;

namespace {

std::vector<RendererLib*> _rendererLibs;
std::map<std::string, RendererLib*> _rendererTypes;

const std::map<std::string, RendererLib*> &rendererTypes()
{
    if (_rendererTypes.empty()) {
        for (auto lib : _rendererLibs) {
            for (const auto &type : lib->types())
                _rendererTypes[type] = lib;
        }
    }
    return _rendererTypes;
}

int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // anonymous namespace

std::atomic<bool> FrameOutside::active{false};
double FrameOutside::phaseMs[FrameOutside::PhaseCount] = {};

void FrameOutside::setEnabled(bool on)
{
    if (active.load(std::memory_order_relaxed) == on)
        return;
    // Zero on the transition, not on drain alone: a scope that ran while
    // the switch was off contributes nothing, but a scope that ran
    // before it was ever drained would otherwise be averaged over a
    // window it does not belong to.
    for (double &v : phaseMs)
        v = 0.0;
    active.store(on, std::memory_order_relaxed);
}

void FrameOutside::add(Phase p, double ms)
{
    // Single-threaded by construction: every caller is on the GUI thread
    // between one backend frame and the next, which is the only thread
    // that draws.
    if (p >= 0 && p < PhaseCount)
        phaseMs[p] += ms;
}

void FrameOutside::drain(double *out)
{
    for (int i = 0; i < PhaseCount; ++i) {
        out[i] = phaseMs[i];
        phaseMs[i] = 0.0;
    }
}

FrameOutsideScope::FrameOutsideScope(FrameOutside::Phase p)
    : phase(p), on(FrameOutside::enabled()), t0(on ? nowNs() : 0)
{}

FrameOutsideScope::~FrameOutsideScope()
{
    stop();
}

void FrameOutsideScope::stop()
{
    if (!on)
        return;
    on = false;
    FrameOutside::add(phase, double(nowNs() - t0) / 1.0e6);
}

void RendererFactory::registerLib(RendererLib *lib)
{
    _rendererTypes.clear();
    _rendererLibs.push_back(lib);
}

std::vector<std::string> RendererFactory::types()
{
    std::vector<std::string> res;
    for (auto &v : rendererTypes())
        res.push_back(v.first);
    return res;
}

bool RendererFactory::warmup(const std::string &type, QOpenGLWidget *widget,
                             RendererLib::WarmupTiming *timing)
{
    if (type.empty() || type == "Default" || !widget)
        return false;
    auto it = rendererTypes().find(type);
    if (it == rendererTypes().end())
        return false;
    return it->second->warmup(widget, type, timing);
}

std::unique_ptr<Renderer> RendererFactory::create(
        const std::string &type, QOpenGLWidget *widget, bool publishOnly)
{
    std::unique_ptr<Renderer> res;
    auto it = rendererTypes().find(type);
    if (it == rendererTypes().end()) {
        if (type.size() && type != "Default")
            qWarning() << "Renderer '" << type.c_str() << "' not supported";
    } else
        res = it->second->create(type, widget, publishOnly);
    return res;
}

static bool _InstancingHint = true;
static int _ActiveCount = 0;
static std::vector<std::function<void()>> _ActivityObservers;

static void notifyActivityObservers()
{
    for (auto &observer : _ActivityObservers)
        observer();
}

Renderer::Renderer()
{
    ++_ActiveCount;
    notifyActivityObservers();
}

Renderer::~Renderer()
{
    --_ActiveCount;
    notifyActivityObservers();
}

int Renderer::activeCount()
{
    return _ActiveCount;
}

void Renderer::addActivityObserver(std::function<void()> observer)
{
    _ActivityObservers.push_back(std::move(observer));
}

void Renderer::setInstancingHint(bool supported)
{
    if (_InstancingHint == supported)
        return;
    _InstancingHint = supported;
    notifyActivityObservers();
}

bool Renderer::instancingHint()
{
    return _InstancingHint;
}

static std::string _ResourcePath;

void RendererFactory::setResourcePath(const std::string &path)
{
    _ResourcePath = path;
    if (_ResourcePath.size() && _ResourcePath.back() != '/' && _ResourcePath.back() != '\\')
        _ResourcePath += "/";
}

const std::string &RendererFactory::resourcePath()
{
    return _ResourcePath;
}

static int _MaxViewIds = 0;

void RendererFactory::setMaxViewIds(int count)
{
    _MaxViewIds = count > 0 ? count : 0;
}

int RendererFactory::maxViewIds()
{
    return _MaxViewIds;
}
