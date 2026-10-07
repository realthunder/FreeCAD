// SPDX-FileCopyrightText: 2026 Benjamin Nauck
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "WindowDecorationButton.h"
#include <QEvent>
#include <QStyle>
#include <QIcon>

WindowDecorationButton::WindowDecorationButton(Role role, QWidget* parent)
    : QPushButton(parent)
    , m_role(role)
{
    setFocusPolicy(Qt::NoFocus);
    setFlat(true);

    // Set a default icon size (QSS can override this via qproperty-iconSize)
    setIconSize(QSize(16, 16));
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

    switch (role) {
        case Minimize:
            setObjectName(QStringLiteral("minimizeButton"));
            break;
        case Maximize:
            setObjectName(QStringLiteral("maximizeButton"));
            break;
        case Restore:
            setObjectName(QStringLiteral("restoreButton"));
            break;
        case Close:
            setObjectName(QStringLiteral("closeButton"));
            break;
    }
    updateIcon();
}

void WindowDecorationButton::updateIcon()
{
    // The glyph that reads against the title bar the way its text does: a
    // light stroke where the text is light. The palette's foreground is the
    // style sheet's `color` once the sheet has polished the widget, and the
    // platform's without a sheet, so this follows a theme switch through the
    // palette change it makes.
    const int light = palette().color(foregroundRole()).lightness() > 127 ? 1 : 0;
    if (light == m_light) {
        return;
    }
    m_light = light;

    static const char* const names[] = {"minimize", "maximize", "restore", "close"};
    setIcon(QIcon(QStringLiteral(":/customtitlebarkit/resources/icons/window-%1%2.svg")
                      .arg(QLatin1String(names[m_role]),
                           light ? QStringLiteral("-dark") : QString())));
}

bool WindowDecorationButton::event(QEvent* e)
{
    const bool handled = QPushButton::event(e);
    switch (e->type()) {
        case QEvent::Polish:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            updateIcon();
            break;
        default:
            break;
    }
    return handled;
}
