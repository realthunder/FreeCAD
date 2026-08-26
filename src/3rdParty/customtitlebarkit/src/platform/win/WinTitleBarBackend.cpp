// SPDX-FileCopyrightText: 2026 Pierre-Louis Boyer
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "WinTitleBarBackend.h"

#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <QMargins>

#include <windows.h>
#include <WinUser.h>
#include <windowsx.h>
#include <dwmapi.h>

namespace
{
// LOCAL DIVERGENCE: how thick the frame Windows expects to be there is, at this
// window's DPI. GetSystemMetrics reports the primary monitor's, which is the
// wrong answer on a second monitor scaled differently.
QMargins resizeBorder(HWND handle)
{
    const UINT dpi = ::GetDpiForWindow(handle);
    const int cx = ::GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
        + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    const int cy = ::GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
        + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return {cx, cy, cx, cy};
}

// LOCAL DIVERGENCE: IsZoomed() alone is a state race. WM_NCCALCSIZE for a
// maximize arrives while the transition is in flight, and WS_MAXIMIZE is not
// reliably set yet -- which is why the inset below never fired. The placement
// is committed earlier, so ask both.
bool isMaximized(HWND handle)
{
    if (::IsZoomed(handle)) {
        return true;
    }
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    return ::GetWindowPlacement(handle, &placement)
        && placement.showCmd == SW_SHOWMAXIMIZED;
}
}  // namespace

// Initialize height to 35 to ensure WM_NCHITTEST works before the first resize event
WinTitleBarBackend::WinTitleBarBackend()
    : m_titleBarHeight(minimumTitleBarHeight())
{}

WinTitleBarBackend::~WinTitleBarBackend() = default;

void WinTitleBarBackend::attach(QWidget* window)
{
    m_window = window;

    // Set frameless window hint to remove native titlebar
    window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint | Qt::Window);

    QTimer::singleShot(10, window, [window]() {
        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        DWORD style = ::GetWindowLong(hwnd, GWL_STYLE);
        ::SetWindowLong(hwnd, GWL_STYLE, style | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_CAPTION);

        // Leave 1 pixel of border so the OS can draw a shadow
        const MARGINS shadow = {1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(hwnd, &shadow);
    });
}

void WinTitleBarBackend::detach()
{
    // LOCAL DIVERGENCE from FreeCAD/FreeCAD#26766: upstream only drops the
    // pointer here, which is enough when detach() happens on the way to
    // destruction. Switching the title bar back to native at run time needs
    // attach() genuinely undone, or the window keeps the frameless hint and the
    // extended frame and comes back with no decoration at all.
    QWidget* window = m_window;
    // Drop the pointer FIRST. handleNativeEvent() answers WM_NCCALCSIZE with
    // "client area == window rect", which is what hides the native frame, and
    // the frame change setWindowFlags() below triggers is delivered
    // synchronously. Leave this set and the very recalculation that restores
    // the non-client area gets swallowed by the handler being undone: the hint
    // and the styles come back, the frame does not, and nothing asks again.
    m_window = nullptr;
    if (window) {
        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        // Zero margins put the client area back where Windows expects it.
        const MARGINS none = {0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(hwnd, &none);
        window->setWindowFlags(window->windowFlags() & ~Qt::FramelessWindowHint);

        // Ask for the recalculation explicitly, and ask for it late.
        //
        // Explicitly, because attach() does not take the frame off through the
        // window style -- it puts WS_CAPTION and WS_THICKFRAME *back* and hides
        // the frame purely by answering WM_NCCALCSIZE. So clearing the hint
        // leaves Qt with no style delta to apply, and no reason to send one.
        //
        // Late, because setWindowFlags() above hid the window, and the show()
        // that follows re-applies the geometry the window had while it was
        // frameless. A frame change asked for here is undone by that show; one
        // queued behind it is not. winId() is re-read for the same reason --
        // the handle may not survive the flag change.
        QPointer<QWidget> alive = window;
        QTimer::singleShot(0, window, [alive]() {
            if (!alive) {
                return;
            }
            HWND handle = reinterpret_cast<HWND>(alive->winId());

            RECT before = {0, 0, 0, 0};
            ::GetClientRect(handle, &before);

            ::SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

            // The frame that just came back is carved out of the client area,
            // so the window has to grow by it. Without this every trip through
            // custom mode costs the content one title bar and two borders, and
            // the window visibly creeps smaller each time it is toggled.
            RECT after = {0, 0, 0, 0};
            RECT outer = {0, 0, 0, 0};
            ::GetClientRect(handle, &after);
            ::GetWindowRect(handle, &outer);
            const int dw = (before.right - before.left) - (after.right - after.left);
            const int dh = (before.bottom - before.top) - (after.bottom - after.top);
            if ((dw != 0 || dh != 0) && !::IsZoomed(handle) && !::IsIconic(handle)) {
                ::SetWindowPos(handle, nullptr, 0, 0,
                               outer.right - outer.left + dw,
                               outer.bottom - outer.top + dh,
                               SWP_NOMOVE | SWP_NOZORDER);
            }
        });
    }
}

QSize WinTitleBarBackend::nativeControlsAreaSize()
{
    // Return {0,0} because the TitleBarWidget manages the Windows control buttons
    // on the right side automatically via its layout. Returning a width here would
    // incorrectly add a macOS-style spacer to the *left* side of the title bar!
    return {0, 0};
}

void WinTitleBarBackend::setTitleBarHeight(int height)
{
    m_titleBarHeight = height;
}

bool WinTitleBarBackend::needsWindowControls() const
{
    // The native titlebar and buttons are hidden by WM_NCCALCSIZE extending
    // the client area. We MUST return true to draw the custom Qt buttons.
    return true;
}

QString WinTitleBarBackend::backendName() const
{
    return QStringLiteral("win");
}

int WinTitleBarBackend::minimumTitleBarHeight() const
{
    return 35;  // Match original FreeCAD implementation
}

int WinTitleBarBackend::snapTitleBarHeight(int requested) const
{
    return requested > 0 ? requested : 35;
}

bool WinTitleBarBackend::handleNativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);

    if (!m_window) {
        return false;
    }

    MSG* msg = reinterpret_cast<MSG*>(message);

    switch (msg->message) {
        case WM_NCCALCSIZE: {
            if (msg->wParam == TRUE) {
                NCCALCSIZE_PARAMS& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);

                // LOCAL DIVERGENCE: maximized, a client area that fills the
                // window rect hangs off the screen. Windows sizes a maximized
                // window to the work area grown by the resize border on every
                // side -- it expects the frame to swallow that -- so with the
                // frame gone the top border's worth of title bar is off the top
                // of the monitor, taking the logo and the window buttons with
                // it. Give the border back on all four sides.
                if (isMaximized(msg->hwnd)) {
                    const QMargins border = resizeBorder(msg->hwnd);
                    params.rgrc[0].left += border.left();
                    params.rgrc[0].top += border.top();
                    params.rgrc[0].right -= border.right();
                    params.rgrc[0].bottom -= border.bottom();
                }
                // Fix a visual bug when resizing: without this, ugly white bands appear
                else if (params.rgrc[0].top != 0) {
                    params.rgrc[0].top -= 1;
                }

                *result = WVR_REDRAW;
                return true;
            }
            return false;
        }

        case WM_NCHITTEST: {
            *result = 0;

            const LONG border_width = 5;
            RECT winrect;
            GetWindowRect(reinterpret_cast<HWND>(m_window->winId()), &winrect);

            long x = GET_X_LPARAM(msg->lParam);
            long y = GET_Y_LPARAM(msg->lParam);

            bool resizeWidth = m_window->minimumWidth() != m_window->maximumWidth();
            bool resizeHeight = m_window->minimumHeight() != m_window->maximumHeight();

            if (resizeWidth) {
                if (x >= winrect.left && x < winrect.left + border_width) {
                    *result = HTLEFT;
                }
                if (x < winrect.right && x >= winrect.right - border_width) {
                    *result = HTRIGHT;
                }
            }
            if (resizeHeight) {
                if (y < winrect.bottom && y >= winrect.bottom - border_width) {
                    *result = HTBOTTOM;
                }
                if (y >= winrect.top && y < winrect.top + border_width) {
                    *result = HTTOP;
                }
            }
            if (resizeWidth && resizeHeight) {
                if (x >= winrect.left && x < winrect.left + border_width && y < winrect.bottom
                    && y >= winrect.bottom - border_width) {
                    *result = HTBOTTOMLEFT;
                }
                if (x < winrect.right && x >= winrect.right - border_width && y < winrect.bottom
                    && y >= winrect.bottom - border_width) {
                    *result = HTBOTTOMRIGHT;
                }
                if (x >= winrect.left && x < winrect.left + border_width && y >= winrect.top
                    && y < winrect.top + border_width) {
                    *result = HTTOPLEFT;
                }
                if (x < winrect.right && x >= winrect.right - border_width && y >= winrect.top
                    && y < winrect.top + border_width) {
                    *result = HTTOPRIGHT;
                }
            }

            if (0 != *result) {
                return true;
            }

            double dpr = m_window->devicePixelRatioF();
            QPoint pos = m_window->mapFromGlobal(
                QPoint(static_cast<int>(x / dpr), static_cast<int>(y / dpr))
            );

            // Check if we're in the titlebar region
            if (pos.y() >= 0 && pos.y() < m_titleBarHeight) {
                QWidget* child = m_window->childAt(pos);

                // Assume the area is draggable unless we hit an interactive widget
                bool isDragArea = true;
                for (auto *widget = child; widget; widget = widget->parentWidget()) {
                    if (widget->property("titleBarDragArea").isValid()) {
                        isDragArea = widget->property("titleBarDragArea").toBool();
                        break;
                    }
                    if (widget->inherits("QAbstractButton") || widget->inherits("QTabBar")
                        || widget->inherits("QMenuBar") || widget->inherits("QComboBox")
                        || widget->inherits("QSpinBox") || widget->inherits("QLineEdit")
                        || widget->inherits("QSlider") || widget->inherits("QScrollBar")
                        || widget->inherits("QToolBar")) {
                        isDragArea = false;  // Block drag so the widget can receive mouse clicks
                        break;
                    }
                }

                if (isDragArea) {
                    *result = HTCAPTION;
                    return true;
                }
            }
            return false;
        }

        case WM_GETMINMAXINFO: {
            // LOCAL DIVERGENCE: upstream insets the window's *contents* by the
            // frame here, to keep a maximized window's content out of the part
            // that hangs off the screen. WM_NCCALCSIZE above now keeps the
            // client area itself inside the work area, which is the same
            // correction one level down -- and the one the title bar is subject
            // to as well, where a contents margin is not. Doing both left an
            // empty border all round a maximized window.
            m_window->setContentsMargins(QMargins());

            // LOCAL DIVERGENCE: ask for the border overhang a maximized window
            // is supposed to have, because this one does not get it.
            //
            // Windows grows a maximized WS_OVERLAPPED window past the work area
            // by the resize border on every side, expecting the frame to
            // swallow it -- that is the overhang WM_NCCALCSIZE above insets
            // back out. attach() makes this window WS_POPUP (Qt's frameless
            // form) and re-adds WS_THICKFRAME behind Qt's back, and a WS_POPUP
            // window is maximized to the work area EXACTLY. So there is no
            // overhang, the inset above has nothing to give back, and the
            // client area ends up equal to the window rect.
            //
            // Qt does not follow: it still books frame margins from the style
            // bits (8px for WS_THICKFRAME) and, when maximized, places
            // geometry() at frameGeometry() inset by them. Its client origin
            // is then 8px inside the real one, and every mapToGlobal /
            // mapFromGlobal in the window is off by that much -- widgets
            // respond 8px diagonally away from where they are drawn.
            //
            // Requesting the overhang makes the inset above land the client
            // area exactly on the work area, which is what Qt already
            // believes. Nothing moves on screen; the two accounts agree.
            HMONITOR monitor = ::MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info = {};
            info.cbSize = sizeof(info);
            if (!::GetMonitorInfoW(monitor, &info)) {
                return false;
            }
            const QMargins border = resizeBorder(msg->hwnd);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(msg->lParam);
            // ptMaxPosition is relative to the monitor's own origin.
            mmi->ptMaxPosition.x = info.rcWork.left - info.rcMonitor.left - border.left();
            mmi->ptMaxPosition.y = info.rcWork.top - info.rcMonitor.top - border.top();
            mmi->ptMaxSize.x =
                (info.rcWork.right - info.rcWork.left) + border.left() + border.right();
            mmi->ptMaxSize.y =
                (info.rcWork.bottom - info.rcWork.top) + border.top() + border.bottom();
            // Without this the max TRACK size caps ptMaxSize back to the
            // work area and the overhang is silently dropped again.
            mmi->ptMaxTrackSize = mmi->ptMaxSize;
            *result = 0;
            return true;
        }

        default:
            return false;
    }
}

std::unique_ptr<PlatformTitleBarBackend> PlatformTitleBarBackend::createPlatform()
{
    return std::make_unique<WinTitleBarBackend>();
}
