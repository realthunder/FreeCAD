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

#ifndef RENDERER_GRAPHEDITOR_IMGUISURFACE_H
#define RENDERER_GRAPHEDITOR_IMGUISURFACE_H

/// \file ImGuiSurface.h
/// A QOpenGLWidget that hosts one Dear ImGui context drawn by the
/// backend (docs/ShaderGraphEditor.md sec 4.3): a standalone
/// Render::DrawSurface with one pass -- the CAM simulator's hosting
/// pattern -- and a Qt-to-ImGui input bridge. A subclass draws its UI
/// in drawUi(), which runs between NewFrame and Render with this
/// surface's context current.
///
/// A frame is drawn on paintGL only. The widget repaints on every
/// input event and for a short tail after it (the node editor
/// animates its navigation), and otherwise costs nothing.
///
/// The backend comes up lazily: while no DrawDevice is up (no 3D view
/// on the renderer path yet) the widget stays blank and asks again on
/// the next paint, the way every facade consumer does.

#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

#include "../DrawSurface.h"

namespace Render {

class ImGuiBgfx;

class RendererExport ImGuiSurface : public QOpenGLWidget {
public:
    explicit ImGuiSurface(QWidget *parent = nullptr);
    ~ImGuiSurface() override;

    /// Ask for a frame; also keeps frames coming for a short while, so
    /// an animation started by the caller finishes.
    void requestFrame();

    /// True once the ImGui context exists (the backend device came up
    /// and the first paint created it).
    bool hasContext() const;

protected:
    /// Draw the UI. Called with this surface's ImGui context current,
    /// between NewFrame and Render.
    virtual void drawUi() = 0;
    /// The ImGui context was just created (before the first drawUi);
    /// a subclass creates what needs a live context here. The
    /// context is current.
    virtual void contextCreated() {}
    /// Make this surface's ImGui context current, for a subclass that
    /// has to talk to ImGui outside drawUi (its destructor, say). No
    /// effect without a context.
    void makeImGuiCurrent();

    void paintGL() override;
    /// Claims key presses from Qt's shortcut map (ShortcutOverride).
    bool event(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void leaveEvent(QEvent *) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    /// Tab belongs to ImGui (navigation, the node search popup), not
    /// to Qt's focus chain.
    bool focusNextPrevChild(bool) override { return false; }

private:
    bool ensureBackend();
    void releaseBackend();
    void syncModifiers(Qt::KeyboardModifiers mods);
    void keyEvent(QKeyEvent *event, bool down);
    void applyCursor();

    std::unique_ptr<DrawSurface> surface;
    std::unique_ptr<ImGuiBgfx> imgui;
    QElapsedTimer clock;
    qint64 lastFrameNs = 0;
    /// Frames keep coming until this many ms after the last input.
    qint64 animateUntilMs = 0;
    QTimer tick;
    int cursorShape = -1;
};

} // namespace Render

#endif // RENDERER_GRAPHEDITOR_IMGUISURFACE_H
