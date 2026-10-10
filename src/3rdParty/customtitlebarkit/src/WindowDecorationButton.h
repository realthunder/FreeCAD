// SPDX-FileCopyrightText: 2026 Benjamin Nauck
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef WINDOWDECORATIONBUTTON_H
#define WINDOWDECORATIONBUTTON_H

#include <QPushButton>

/// A borderless window control button.
/// Themeable via QSS — object names: "minimizeButton", "maximizeButton", "restoreButton", "closeButton".
class WindowDecorationButton: public QPushButton
{
    Q_OBJECT

public:
    enum Role
    {
        Minimize,
        Maximize,
        Restore,
        Close
    };

    explicit WindowDecorationButton(Role role, QWidget* parent = nullptr);

protected:
    /// LOCAL DIVERGENCE from FreeCAD/FreeCAD#26766: the glyph follows the
    /// text colour. The kit ships each glyph twice, a dark stroke and a
    /// light one, and upstream loads the dark one whatever the theme, so on
    /// a dark title bar the three buttons all but vanish.
    bool event(QEvent* e) override;

private:
    void updateIcon();

    Role m_role;
    /// Which glyph is loaded: 1 the light stroke, 0 the dark, -1 none yet.
    int m_light = -1;
};

#endif  // WINDOWDECORATIONBUTTON_H