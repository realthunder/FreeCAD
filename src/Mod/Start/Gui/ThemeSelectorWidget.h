// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 The FreeCAD Project Association AISBL               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#ifndef FREECAD_START_THEMESELECTORWIDGET_H
#define FREECAD_START_THEMESELECTORWIDGET_H

#include <QIcon>
#include <QString>
#include <QWidget>
#include <vector>

class QBoxLayout;
class QLabel;
class QToolButton;

namespace StartGui
{

/// A widget to allow selection of the UI theme (color scheme).
class ThemeSelectorWidget: public QWidget
{
    Q_OBJECT
public:
    explicit ThemeSelectorWidget(QWidget* parent = nullptr);
    bool eventFilter(QObject* object, QEvent* event) override;

protected:
    /** Apply a theme.
     * \param packName the preference pack to apply, empty for Match Desktop,
     *                 which resolves to whichever pack matches the desktop.
     */
    void themeChanged(const QString& packName);

private:
    void retranslateUi();
    void setupUi();
    void setupButtons(QBoxLayout* layout);
    void onLinkActivated(const QString& link);

    /// One button, and the theme pack it applies. An empty name is Match Desktop.
    struct ThemeButton
    {
        QString packName;
        QToolButton* button;
    };

    /// The picture for a theme: a shipped thumbnail where there is one, and
    /// otherwise a swatch drawn from the light or dark scheme the theme pins.
    static QIcon iconForTheme(const QString& packName);

    QLabel* _titleLabel;
    QLabel* _descriptionLabel;
    std::vector<ThemeButton> _buttons;
};

}  // namespace StartGui

#endif  // FREECAD_START_THEMESELECTORWIDGET_H
