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

#include "ImGuiSurface.h"
#include "ImGuiBgfx.h"

#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <dear-imgui/imgui.h>

#include <cfloat>

namespace {

/// How long after the last input the surface keeps drawing frames:
/// long enough for the node editor's smooth zoom and pan to settle.
constexpr qint64 kAnimateTailMs = 300;
constexpr int kFrameIntervalMs = 16;
constexpr float kFontSize = 16.0f;

ImGuiKey toImGuiKey(int key)
{
    switch (key) {
    case Qt::Key_Tab: return ImGuiKey_Tab;
    case Qt::Key_Left: return ImGuiKey_LeftArrow;
    case Qt::Key_Right: return ImGuiKey_RightArrow;
    case Qt::Key_Up: return ImGuiKey_UpArrow;
    case Qt::Key_Down: return ImGuiKey_DownArrow;
    case Qt::Key_PageUp: return ImGuiKey_PageUp;
    case Qt::Key_PageDown: return ImGuiKey_PageDown;
    case Qt::Key_Home: return ImGuiKey_Home;
    case Qt::Key_End: return ImGuiKey_End;
    case Qt::Key_Insert: return ImGuiKey_Insert;
    case Qt::Key_Delete: return ImGuiKey_Delete;
    case Qt::Key_Backspace: return ImGuiKey_Backspace;
    case Qt::Key_Space: return ImGuiKey_Space;
    case Qt::Key_Return: return ImGuiKey_Enter;
    case Qt::Key_Enter: return ImGuiKey_KeypadEnter;
    case Qt::Key_Escape: return ImGuiKey_Escape;
    case Qt::Key_Control: return ImGuiKey_LeftCtrl;
    case Qt::Key_Shift: return ImGuiKey_LeftShift;
    case Qt::Key_Alt: return ImGuiKey_LeftAlt;
    case Qt::Key_Meta: return ImGuiKey_LeftSuper;
    case Qt::Key_Menu: return ImGuiKey_Menu;
    case Qt::Key_Apostrophe: return ImGuiKey_Apostrophe;
    case Qt::Key_Comma: return ImGuiKey_Comma;
    case Qt::Key_Minus: return ImGuiKey_Minus;
    case Qt::Key_Period: return ImGuiKey_Period;
    case Qt::Key_Slash: return ImGuiKey_Slash;
    case Qt::Key_Semicolon: return ImGuiKey_Semicolon;
    case Qt::Key_Equal: return ImGuiKey_Equal;
    case Qt::Key_BracketLeft: return ImGuiKey_LeftBracket;
    case Qt::Key_Backslash: return ImGuiKey_Backslash;
    case Qt::Key_BracketRight: return ImGuiKey_RightBracket;
    case Qt::Key_QuoteLeft: return ImGuiKey_GraveAccent;
    case Qt::Key_CapsLock: return ImGuiKey_CapsLock;
    case Qt::Key_ScrollLock: return ImGuiKey_ScrollLock;
    case Qt::Key_NumLock: return ImGuiKey_NumLock;
    case Qt::Key_Print: return ImGuiKey_PrintScreen;
    case Qt::Key_Pause: return ImGuiKey_Pause;
    case Qt::Key_F1: return ImGuiKey_F1;
    case Qt::Key_F2: return ImGuiKey_F2;
    case Qt::Key_F3: return ImGuiKey_F3;
    case Qt::Key_F4: return ImGuiKey_F4;
    case Qt::Key_F5: return ImGuiKey_F5;
    case Qt::Key_F6: return ImGuiKey_F6;
    case Qt::Key_F7: return ImGuiKey_F7;
    case Qt::Key_F8: return ImGuiKey_F8;
    case Qt::Key_F9: return ImGuiKey_F9;
    case Qt::Key_F10: return ImGuiKey_F10;
    case Qt::Key_F11: return ImGuiKey_F11;
    case Qt::Key_F12: return ImGuiKey_F12;
    default:
        break;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return ImGuiKey(ImGuiKey_0 + (key - Qt::Key_0));
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return ImGuiKey(ImGuiKey_A + (key - Qt::Key_A));
    return ImGuiKey_None;
}

int toImGuiButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return ImGuiMouseButton_Left;
    case Qt::RightButton: return ImGuiMouseButton_Right;
    case Qt::MiddleButton: return ImGuiMouseButton_Middle;
    default: return -1;
    }
}

Qt::CursorShape toQtCursor(ImGuiMouseCursor cursor)
{
    switch (cursor) {
    case ImGuiMouseCursor_TextInput: return Qt::IBeamCursor;
    case ImGuiMouseCursor_ResizeAll: return Qt::SizeAllCursor;
    case ImGuiMouseCursor_ResizeNS: return Qt::SizeVerCursor;
    case ImGuiMouseCursor_ResizeEW: return Qt::SizeHorCursor;
    case ImGuiMouseCursor_ResizeNESW: return Qt::SizeBDiagCursor;
    case ImGuiMouseCursor_ResizeNWSE: return Qt::SizeFDiagCursor;
    case ImGuiMouseCursor_Hand: return Qt::PointingHandCursor;
    case ImGuiMouseCursor_NotAllowed: return Qt::ForbiddenCursor;
    case ImGuiMouseCursor_None: return Qt::BlankCursor;
    default: return Qt::ArrowCursor;
    }
}

} // namespace

namespace Render {

ImGuiSurface::ImGuiSurface(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_InputMethodEnabled);
    clock.start();
    tick.setSingleShot(true);
    tick.setInterval(kFrameIntervalMs);
    QObject::connect(&tick, &QTimer::timeout, this,
                     [this]() { update(); });
}

ImGuiSurface::~ImGuiSurface()
{
    releaseBackend();
}

bool ImGuiSurface::hasContext() const
{
    return imgui && imgui->valid();
}

void ImGuiSurface::makeImGuiCurrent()
{
    if (imgui)
        imgui->makeCurrent();
}

void ImGuiSurface::requestFrame()
{
    animateUntilMs = clock.elapsed() + kAnimateTailMs;
    update();
}

bool ImGuiSurface::ensureBackend()
{
    if (!DrawDevice::instance()) {
        // Device gone or never up: surface handles died with it.
        releaseBackend();
        return false;
    }
    if (!surface) {
        surface = DrawSurface::create(this, 1);
        if (!surface)
            return false;
        surface->setPassClear(0, 0x1e1e1eff, 1.0f, 0,
                              ClearColor | ClearDepth);
    }
    return true;
}

void ImGuiSurface::releaseBackend()
{
    if (imgui) {
        imgui->destroy(DrawDevice::instance() != nullptr);
        imgui.reset();
    }
    surface.reset();
}

void ImGuiSurface::paintGL()
{
    if (!ensureBackend())
        return;
    const qreal ratio = devicePixelRatioF();
    const int fbWidth = int(width() * ratio);
    const int fbHeight = int(height() * ratio);
    if (!surface->beginFrame(fbWidth, fbHeight))
        return;

    if (!imgui) {
        imgui = std::make_unique<ImGuiBgfx>();
        if (!imgui->create(kFontSize)) {
            imgui.reset();
            surface->endFrame();
            return;
        }
        imgui->makeCurrent();
        contextCreated();
    }

    const qint64 nowNs = clock.nsecsElapsed();
    const float dt = lastFrameNs ? float(nowNs - lastFrameNs) * 1e-9f : 0.0f;
    lastFrameNs = nowNs;
    imgui->newFrame(float(width()), float(height()), float(ratio), dt);
    drawUi();
    imgui->render(surface->nativePassId(0), fbWidth, fbHeight);
    surface->endFrame();

    applyCursor();
    // Keep drawing while something is live: a held button, an active
    // widget (a text field's caret), or the tail after the last input.
    imgui->makeCurrent();
    const ImGuiIO &io = ImGui::GetIO();
    const bool live = ImGui::IsAnyMouseDown() || ImGui::IsAnyItemActive()
        || io.WantTextInput || clock.elapsed() < animateUntilMs;
    if (live && !tick.isActive())
        tick.start();
}

void ImGuiSurface::applyCursor()
{
    imgui->makeCurrent();
    const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (cursor == cursorShape)
        return;
    cursorShape = cursor;
    setCursor(toQtCursor(cursor));
}

void ImGuiSurface::syncModifiers(Qt::KeyboardModifiers mods)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, mods.testFlag(Qt::ControlModifier));
    io.AddKeyEvent(ImGuiMod_Shift, mods.testFlag(Qt::ShiftModifier));
    io.AddKeyEvent(ImGuiMod_Alt, mods.testFlag(Qt::AltModifier));
    io.AddKeyEvent(ImGuiMod_Super, mods.testFlag(Qt::MetaModifier));
}

void ImGuiSurface::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (hasContext()) {
        imgui->makeCurrent();
        syncModifiers(event->modifiers());
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(float(event->position().x()),
                            float(event->position().y()));
        const int button = toImGuiButton(event->button());
        if (button >= 0)
            io.AddMouseButtonEvent(button, true);
    }
    event->accept();
    requestFrame();
}

void ImGuiSurface::mouseReleaseEvent(QMouseEvent *event)
{
    if (hasContext()) {
        imgui->makeCurrent();
        syncModifiers(event->modifiers());
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(float(event->position().x()),
                            float(event->position().y()));
        const int button = toImGuiButton(event->button());
        if (button >= 0)
            io.AddMouseButtonEvent(button, false);
    }
    event->accept();
    requestFrame();
}

void ImGuiSurface::mouseDoubleClickEvent(QMouseEvent *event)
{
    // ImGui detects double clicks itself from the press timing; Qt
    // replaces the second press by this event, so hand it on as one.
    mousePressEvent(event);
}

void ImGuiSurface::mouseMoveEvent(QMouseEvent *event)
{
    if (hasContext()) {
        imgui->makeCurrent();
        syncModifiers(event->modifiers());
        ImGui::GetIO().AddMousePosEvent(float(event->position().x()),
                                        float(event->position().y()));
    }
    event->accept();
    requestFrame();
}

void ImGuiSurface::wheelEvent(QWheelEvent *event)
{
    if (hasContext()) {
        imgui->makeCurrent();
        syncModifiers(event->modifiers());
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(float(event->position().x()),
                            float(event->position().y()));
        const QPoint delta = event->angleDelta();
        io.AddMouseWheelEvent(float(delta.x()) / 120.0f,
                              float(delta.y()) / 120.0f);
    }
    event->accept();
    requestFrame();
}

void ImGuiSurface::keyEvent(QKeyEvent *event, bool down)
{
    if (!hasContext())
        return;
    imgui->makeCurrent();
    syncModifiers(event->modifiers());
    ImGuiIO &io = ImGui::GetIO();
    const ImGuiKey key = toImGuiKey(event->key());
    if (key != ImGuiKey_None) {
        io.AddKeyEvent(key, down);
        io.SetKeyEventNativeData(key, event->key(),
                                 int(event->nativeScanCode()));
    }
    if (down && !event->modifiers().testFlag(Qt::ControlModifier)) {
        const QString text = event->text();
        if (!text.isEmpty() && text.at(0).isPrint())
            io.AddInputCharactersUTF8(text.toUtf8().constData());
    }
}

bool ImGuiSurface::event(QEvent *event)
{
    // Qt's shortcut map sees a key press before the widget does, and
    // a key that is the first of a multi-key shortcut sequence (the
    // application has many) is consumed there: the widget gets the
    // release and never the press. A text widget claims its keys by
    // accepting ShortcutOverride, which is what this does, with the
    // same reach as QLineEdit's: everything while a text field is
    // being typed into, everything while an item is being dragged or
    // edited, and any unmodified key otherwise (Delete, Tab, Space,
    // arrows and letters drive the canvas). A Ctrl/Alt/Meta chord
    // with no text field active stays with the application, so undo,
    // redo and save keep meaning what they mean everywhere else.
    if (event->type() == QEvent::ShortcutOverride && hasContext()) {
        auto *ke = static_cast<QKeyEvent *>(event);
        imgui->makeCurrent();
        const ImGuiIO &io = ImGui::GetIO();
        const bool chord = ke->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (io.WantTextInput || ImGui::IsAnyItemActive() || !chord) {
            event->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(event);
}

void ImGuiSurface::keyPressEvent(QKeyEvent *event)
{
    keyEvent(event, true);
    event->accept();
    requestFrame();
}

void ImGuiSurface::keyReleaseEvent(QKeyEvent *event)
{
    keyEvent(event, false);
    event->accept();
    requestFrame();
}

void ImGuiSurface::inputMethodEvent(QInputMethodEvent *event)
{
    if (hasContext() && !event->commitString().isEmpty()) {
        imgui->makeCurrent();
        ImGui::GetIO().AddInputCharactersUTF8(
                event->commitString().toUtf8().constData());
    }
    event->accept();
    requestFrame();
}

void ImGuiSurface::focusInEvent(QFocusEvent *event)
{
    QOpenGLWidget::focusInEvent(event);
    if (hasContext()) {
        imgui->makeCurrent();
        ImGui::GetIO().AddFocusEvent(true);
    }
    requestFrame();
}

void ImGuiSurface::focusOutEvent(QFocusEvent *event)
{
    QOpenGLWidget::focusOutEvent(event);
    if (hasContext()) {
        imgui->makeCurrent();
        ImGui::GetIO().AddFocusEvent(false);
    }
    requestFrame();
}

void ImGuiSurface::leaveEvent(QEvent *event)
{
    QOpenGLWidget::leaveEvent(event);
    if (hasContext()) {
        imgui->makeCurrent();
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    requestFrame();
}

} // namespace Render
