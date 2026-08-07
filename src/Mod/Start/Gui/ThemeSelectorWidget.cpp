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

#include "PreCompiled.h"
#ifndef _PreComp_
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QToolButton>
#endif

#include "ThemeSelectorWidget.h"
#include <App/Application.h>
#include <Gui/Command.h>
#include <Gui/PreferencePackManager.h>

using namespace StartGui;

ThemeSelectorWidget::ThemeSelectorWidget(QWidget* parent)
    : QWidget(parent)
    , _titleLabel {nullptr}
    , _descriptionLabel {nullptr}
    , _buttons {nullptr, nullptr, nullptr, nullptr}
{
    setObjectName(QLatin1String("ThemeSelectorWidget"));
    setupUi();
    qApp->installEventFilter(this);
}


void ThemeSelectorWidget::setupButtons(QBoxLayout* layout)
{
    if (!layout) {
        return;
    }
    std::map<Theme, QString> themeMap {{Theme::Classic, tr("FreeCAD Classic")},
                                       {Theme::Auto, tr("Match Desktop")},
                                       {Theme::Dark, tr("FreeCAD Dark")},
                                       {Theme::Light, tr("FreeCAD Light")}};
    std::map<Theme, QIcon> iconMap {
        {Theme::Classic, QIcon(QLatin1String(":/thumbnails/Theme_thumbnail_classic.png"))},
        {Theme::Auto, QIcon(QLatin1String(":/thumbnails/Theme_thumbnail_auto.png"))},
        {Theme::Light, QIcon(QLatin1String(":/thumbnails/Theme_thumbnail_light.png"))},
        {Theme::Dark, QIcon(QLatin1String(":/thumbnails/Theme_thumbnail_dark.png"))}};
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");
    auto styleSheetName = QString::fromStdString(hGrp->GetASCII("StyleSheet"));
    // Auto keeps the stylesheet of whichever theme it resolved to, so it is
    // recognizable only by its own marker and has to be tested first. The rest
    // are told apart by the stylesheet the pack leaves behind (Dark.qss,
    // Light.qss, ...), which is what the parameter actually holds.
    const Theme activeTheme = [&hGrp, &styleSheetName] {
        if (hGrp->GetBool("ThemeAuto", false)) {
            return Theme::Auto;
        }
        if (styleSheetName.contains(QLatin1String("Light"), Qt::CaseSensitivity::CaseInsensitive)) {
            return Theme::Light;
        }
        if (styleSheetName.contains(QLatin1String("Dark"), Qt::CaseSensitivity::CaseInsensitive)) {
            return Theme::Dark;
        }
        return Theme::Classic;  // the theme without a stylesheet of its own
    }();
    for (const auto& theme : themeMap) {
        auto button = new QToolButton();
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonTextUnderIcon);
        button->setText(theme.second);
        button->setIcon(iconMap[theme.first]);
        button->setIconSize(iconMap[theme.first].actualSize(QSize(256, 256)));
        if (theme.first == activeTheme) {
            button->setChecked(true);
        }
        connect(button, &QToolButton::clicked, this, [this, theme] {
            themeChanged(theme.first);
        });
        layout->addWidget(button);
        _buttons[static_cast<int>(theme.first)] = button;
    }
}

void ThemeSelectorWidget::setupUi()
{
    auto* outerLayout = new QVBoxLayout(this);
    auto* buttonLayout = new QHBoxLayout;
    _titleLabel = new QLabel;
    _descriptionLabel = new QLabel;
    outerLayout->addWidget(_titleLabel);
    outerLayout->addLayout(buttonLayout);
    outerLayout->addWidget(_descriptionLabel);
    setupButtons(buttonLayout);
    retranslateUi();
    connect(_descriptionLabel, &QLabel::linkActivated, this, &ThemeSelectorWidget::onLinkActivated);
}

void ThemeSelectorWidget::onLinkActivated(const QString& link)
{
    auto const addonManagerLink = QStringLiteral("freecad:Std_AddonMgr");

    if (link != addonManagerLink) {
        return;
    }

    // Set the user preferences to include only preference packs.
    // This is a quick and dirty way to open Addon Manager with only themes.
    auto pref =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Addons");
    pref->SetInt("PackageTypeSelection", 3);  // 3 stands for Preference Packs
    pref->SetInt("StatusSelection", 0);       // 0 stands for any installation status

    Gui::Application::Instance->commandManager().runCommandByName("Std_AddonMgr");
}

void ThemeSelectorWidget::themeChanged(Theme newTheme)
{
    // Run the appropriate preference pack. The names are those in
    // Gui/PreferencePacks/package.xml, which this fork renamed away from
    // upstream's "FreeCAD "-prefixed ones.
    auto prefPackManager = Gui::Application::Instance->prefPackManager();
    // Auto has no pack of its own; it resolves to the one matching the desktop.
    const bool isAuto = newTheme == Theme::Auto;
    const bool wantDark =
        newTheme == Theme::Dark || (isAuto && Gui::Application::systemPrefersDarkScheme());
    switch (newTheme) {
        case Theme::Classic:
            prefPackManager->apply("Classic");
            break;
        case Theme::Dark:
            prefPackManager->apply("Dark");
            break;
        case Theme::Light:
            prefPackManager->apply("Light");
            break;
        case Theme::Auto:
            prefPackManager->apply(wantDark ? "Dark" : "Light");
            break;
    }

    auto hMainWindow = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");
    // The pack has already written ColorScheme, and writing it is enough to
    // repaint: DlgSettingsGeneral observes that key and re-applies palette and
    // stylesheet, the same way it does for a stylesheet change, so no restart is
    // needed here either. Auto only adds a marker saying which way it resolved,
    // so the next start can re-resolve if the desktop changed meanwhile; the
    // palette stays pinned to the resolved scheme, keeping it consistent with
    // the stylesheet the pack applied.
    hMainWindow->SetBool("ThemeAuto", isAuto);
    if (isAuto) {
        hMainWindow->SetASCII("ThemeAutoApplied", wantDark ? "Dark" : "Light");
    }

    ParameterGrp::handle hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Themes");
    const unsigned long nonExistentColor = -1434171135;
    const unsigned long defaultAccentColor = 1434171135;
    unsigned long longAccentColor1 = hGrp->GetUnsigned("ThemeAccentColor1", nonExistentColor);
    if (longAccentColor1 == nonExistentColor) {
        hGrp->SetUnsigned("ThemeAccentColor1", defaultAccentColor);
        hGrp->SetUnsigned("ThemeAccentColor2", defaultAccentColor);
        hGrp->SetUnsigned("ThemeAccentColor3", defaultAccentColor);
    }
}

bool ThemeSelectorWidget::eventFilter(QObject* object, QEvent* event)
{
    if (object == this && event->type() == QEvent::LanguageChange) {
        this->retranslateUi();
    }
    return QWidget::eventFilter(object, event);
}

void ThemeSelectorWidget::retranslateUi()
{
    _titleLabel->setText(QLatin1String("<h2>") + tr("Theme") + QLatin1String("</h2>"));
    _descriptionLabel->setText(tr("Looking for more themes? You can obtain them using "
                                  "<a href=\"freecad:Std_AddonMgr\">Addon Manager</a>."));
    _buttons[static_cast<int>(Theme::Dark)]->setText(tr("FreeCAD Dark", "Visual theme name"));
    _buttons[static_cast<int>(Theme::Light)]->setText(tr("FreeCAD Light", "Visual theme name"));
    _buttons[static_cast<int>(Theme::Classic)]->setText(tr("FreeCAD Classic", "Visual theme name"));
    _buttons[static_cast<int>(Theme::Auto)]->setText(tr("Match Desktop", "Visual theme name"));
}
