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
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QToolButton>
#endif

#include "ThemeSelectorWidget.h"
#include <App/Application.h>
#include <Base/Parameter.h>
#include <Gui/Command.h>
#include <Gui/PreferencePackManager.h>
#include <Gui/ThemeManager.h>

using namespace StartGui;

namespace
{
/// The thumbnails shipped for the themes this page used to be limited to.
QString shippedThumbnail(const QString& packName)
{
    if (packName.isEmpty()) {
        return QStringLiteral(":/thumbnails/Theme_thumbnail_auto.png");
    }
    if (packName == QLatin1String("Classic")) {
        return QStringLiteral(":/thumbnails/Theme_thumbnail_classic.png");
    }
    if (packName == QLatin1String("Light")) {
        return QStringLiteral(":/thumbnails/Theme_thumbnail_light.png");
    }
    if (packName == QLatin1String("Dark")) {
        return QStringLiteral(":/thumbnails/Theme_thumbnail_dark.png");
    }
    return {};
}

/// Which way a theme pins the palette, read from the pack itself.
bool themeIsDark(const QString& packName)
{
    const auto configFile =
        Gui::Application::Instance->prefPackManager()->configFileFor(packName.toStdString());
    if (configFile.empty()) {
        return false;
    }

    auto parameters = ParameterManager::Create();
    parameters->LoadDocument(configFile.string().c_str());
    Base::Reference<ParameterGrp> group(parameters);
    for (const char* name : {"BaseApp", "Preferences", "MainWindow"}) {
        if (!group->HasGroup(name)) {
            return false;
        }
        group = group->GetGroup(name);
    }
    return group->GetASCII("ColorScheme") == "Dark";
}

/// A window-ish swatch in the theme's own light or dark, for a theme that
/// ships no thumbnail of its own.
QIcon drawSwatch(const QSize& size, bool dark)
{
    const QColor paper = dark ? QColor(0x2b, 0x2e, 0x33) : QColor(0xf4, 0xf4, 0xf5);
    const QColor ink = dark ? QColor(0x60, 0x64, 0x6a) : QColor(0xb4, 0xb4, 0xb8);

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF frame(1.5, 1.5, size.width() - 3.0, size.height() - 3.0);
    painter.setPen(QPen(ink, 2.0));
    painter.setBrush(paper);
    painter.drawRoundedRect(frame, 6.0, 6.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawRect(QRectF(frame.left() + 1.0,
                            frame.top() + 1.0,
                            frame.width() - 2.0,
                            std::max(4.0, frame.height() * 0.18)));

    return QIcon(pixmap);
}
}  // namespace

ThemeSelectorWidget::ThemeSelectorWidget(QWidget* parent)
    : QWidget(parent)
    , _titleLabel {nullptr}
    , _descriptionLabel {nullptr}
{
    setObjectName(QLatin1String("ThemeSelectorWidget"));
    setupUi();
    qApp->installEventFilter(this);
}

QIcon ThemeSelectorWidget::iconForTheme(const QString& packName)
{
    const QString shipped = shippedThumbnail(packName);
    if (!shipped.isEmpty()) {
        return QIcon(shipped);
    }

    // Match whatever the shipped thumbnails are, so a row of mixed buttons
    // still lines up.
    static const QSize size =
        QIcon(QStringLiteral(":/thumbnails/Theme_thumbnail_auto.png")).actualSize(QSize(256, 256));
    return drawSwatch(size, themeIsDark(packName));
}

void ThemeSelectorWidget::setupButtons(QBoxLayout* layout)
{
    if (!layout) {
        return;
    }

    // Match Desktop has no pack of its own and comes first; everything the
    // installation declares as a theme follows, so a theme from the Addon
    // Manager is offered here the same way the shipped ones are.
    std::vector<QString> packNames {QString()};
    for (const auto& pack : Gui::Application::Instance->prefPackManager()->preferencePacks()) {
        if (pack.second.metadata().type() == "Theme") {
            packNames.push_back(QString::fromStdString(pack.first));
        }
    }

    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");
    // Auto keeps the stylesheet of whichever theme it resolved to, so it is
    // recognizable only by its own marker and has to be tested first.
    const QString activeTheme = [&hGrp] {
        if (hGrp->GetBool("ThemeAuto", false)) {
            return QString();
        }
        const QString theme = QString::fromStdString(Gui::ThemeManager::currentTheme());
        if (!theme.isEmpty()) {
            return theme;
        }
        // A config written before the applied pack was recorded carries only
        // the stylesheet the pack left behind (Dark.qss, Light.qss, ...).
        const auto styleSheet = QString::fromStdString(hGrp->GetASCII("StyleSheet"));
        if (styleSheet.contains(QLatin1String("Light"), Qt::CaseInsensitive)) {
            return QStringLiteral("Light");
        }
        if (styleSheet.contains(QLatin1String("Dark"), Qt::CaseInsensitive)) {
            return QStringLiteral("Dark");
        }
        return QStringLiteral("Classic");  // the theme without a stylesheet of its own
    }();

    for (const auto& packName : packNames) {
        auto button = new QToolButton();
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonTextUnderIcon);
        button->setText(packName.isEmpty() ? tr("Match Desktop", "Visual theme name") : packName);
        const QIcon icon = iconForTheme(packName);
        button->setIcon(icon);
        button->setIconSize(icon.actualSize(QSize(256, 256)));
        if (packName == activeTheme) {
            button->setChecked(true);
        }
        connect(button, &QToolButton::clicked, this, [this, packName] {
            themeChanged(packName);
        });
        layout->addWidget(button);
        _buttons.push_back({packName, button});
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

void ThemeSelectorWidget::themeChanged(const QString& packName)
{
    auto prefPackManager = Gui::Application::Instance->prefPackManager();
    // Auto has no pack of its own; it resolves to the one matching the desktop.
    const bool isAuto = packName.isEmpty();
    const bool wantDark = isAuto && Gui::Application::systemPrefersDarkScheme();
    if (isAuto) {
        prefPackManager->apply(wantDark ? "Dark" : "Light");
    }
    else {
        prefPackManager->apply(packName.toStdString());
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

    // Every other button is named after the pack it applies, which is a name
    // the installation chose rather than one to translate.
    for (const auto& entry : _buttons) {
        if (entry.packName.isEmpty()) {
            entry.button->setText(tr("Match Desktop", "Visual theme name"));
        }
    }
}
