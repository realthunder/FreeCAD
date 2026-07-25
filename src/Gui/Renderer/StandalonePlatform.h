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

#ifndef RENDERER_STANDALONE_PLATFORM_H
#define RENDERER_STANDALONE_PLATFORM_H

/// Qt-free stand-ins for the standalone (no-GUI) renderer build — today
/// the Emscripten/WebAssembly viewer. Compiled in when
/// FC_RENDERER_STANDALONE is defined: the renderer then owns its window
/// (bgfx creates the WebGL context on the canvas) instead of sharing
/// Qt's, and the few Qt types/functions the renderer interface touches
/// resolve to the minimal equivalents below.

#ifdef FC_RENDERER_STANDALONE

#include <cstdio>
#include <sstream>

#include <bgfx/bgfx.h>
#include <bx/string.h>

class QOpenGLWidget;   // opaque: the standalone build passes nullptr

/// Minimal QColor stand-in: just the packed-rgb accessors render() reads.
class QColor
{
public:
    QColor() = default;
    QColor(int r, int g, int b, int a = 255)
        : m_r(r), m_g(g), m_b(b), m_a(a)
    {}
    int red() const { return m_r; }
    int green() const { return m_g; }
    int blue() const { return m_b; }
    int alpha() const { return m_a; }

private:
    int m_r = 0;
    int m_g = 0;
    int m_b = 0;
    int m_a = 255;
};

namespace RenderStandalone {

/// One-statement stderr logger emulating the qDebug()/qCritical()
/// stream interface the renderer's log macros use.
class LogStream
{
public:
    explicit LogStream(const char *level)
    {
        m_ss << level << ": ";
    }
    ~LogStream()
    {
        std::fprintf(stderr, "%s\n", m_ss.str().c_str());
    }
    template<typename T>
    LogStream &operator<<(const T &v)
    {
        m_ss << v << ' ';
        return *this;
    }

private:
    std::ostringstream m_ss;
};

} // namespace RenderStandalone

inline RenderStandalone::LogStream qDebug()
{
    return RenderStandalone::LogStream("bgfx debug");
}
inline RenderStandalone::LogStream qWarning()
{
    return RenderStandalone::LogStream("bgfx warning");
}
inline RenderStandalone::LogStream qCritical()
{
    return RenderStandalone::LogStream("bgfx error");
}

template<typename T>
inline const T &qMin(const T &a, const T &b)
{
    return a < b ? a : b;
}

template<typename T>
inline const T &qMax(const T &a, const T &b)
{
    return a < b ? b : a;
}

/// bgfx example-common (bgfx_utils) replacement: load a compiled shader
/// pair from <path>/shaders/<api>/<name>.bin with plain stdio.
bgfx::ProgramHandle loadProgram(const bx::StringView &vsName,
                                const bx::StringView &fsName,
                                const char *path = nullptr);
/// Single-shader flavor of the same loader (a stock vertex stage to
/// pair with a snapshot-shipped user fragment binary).
bgfx::ShaderHandle loadShader(const bx::StringView &name,
                              const char *path = nullptr);

#endif // FC_RENDERER_STANDALONE

#endif // RENDERER_STANDALONE_PLATFORM_H
