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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cmath>
# include <QCheckBox>
# include <QColorDialog>
# include <QComboBox>
# include <QGridLayout>
# include <QHBoxLayout>
# include <QLabel>
# include <QApplication>
# include <QMenu>
# include <QMouseEvent>
# include <QPixmap>
# include <QPointer>
# include <QPushButton>
# include <QSlider>
# include <QToolButton>
# include <QWidgetAction>
#endif

#include <App/PropertyStandard.h>
#include <App/PropertyGeo.h>
#include <Base/Tools.h>

#include "DisplayOptions.h"
#include "Action.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "FileDialog.h"
#include "MainWindow.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewParams.h"

using namespace Gui;

namespace {

// What marks a row of the menu as one this section stands in for
const char *HiddenRow = "FCDrawStyleRowHidden";

View3DInventorViewer *viewerInFront()
{
    auto view = qobject_cast<View3DInventor*>(Application::Instance->activeView());
    return view ? view->getViewer() : nullptr;
}

std::vector<Command*> styleCommands()
{
    auto cmd = Application::Instance->commandManager().getCommandByName("Std_DrawStyle");
    if (auto group = dynamic_cast<GroupCommand*>(cmd))
        return group->getCommands();
    return {};
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto title = new QLabel(text, parent);
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);
    return title;
}

QString doc(const char *text)
{
    return QString::fromUtf8(text);
}

// Dismiss the menu a section sits in, the way a click beside it does: a
// mouse press far outside its rectangle, which QMenu answers by taking
// down the whole chain up to the menu bar and leaving the bar's keyboard
// mode. Hiding or closing the popup by hand does the half of that, and the
// menu bar then swallows the next click on it (docs/CoinRetirement.md
// sec 3.5).
void dismiss(QWidget *section)
{
    auto menu = qobject_cast<QMenu*>(section ? section->parentWidget() : nullptr);
    if (!menu || !menu->isVisible())
        return;
    const QPoint away(-10000, -10000);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(away), QPointF(menu->mapToGlobal(away)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(menu, &press);
}

} // anonymous namespace

// ---------------------------------------------------------------------------

DrawStyleOptionsWidget::DrawStyleOptionsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(12, 6, 12, 4);
    layout->setHorizontalSpacing(12);

    styleCombo = new QComboBox(this);
    styleCombo->setObjectName(QStringLiteral("DrawStyleCombo"));
    styleCombo->setToolTip(tr("How the active 3D view draws its objects, whatever display "
                              "mode each of them is in."));
    for (auto cmd : styleCommands()) {
        styleCombo->addItem(BitmapFactory().iconFromTheme(cmd->getPixmap()),
                            Action::commandMenuText(cmd),
                            QByteArray(cmd->getName()));
        styleCombo->setItemData(styleCombo->count() - 1, Action::commandToolTip(cmd, false),
                                Qt::ToolTipRole);
    }
    layout->addWidget(new QLabel(tr("Display style:"), this), 0, 0);
    layout->addWidget(styleCombo, 0, 1);
    connect(styleCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        // The style's own command, as its shortcut and a macro run it:
        // the active view's style, every view's with Ctrl held.
        const QByteArray name = styleCombo->itemData(index).toByteArray();
        if (!name.isEmpty())
            Application::Instance->commandManager().runCommandByName(name.constData());
        refresh();
    });

    aliasingCombo = new QComboBox(this);
    aliasingCombo->setObjectName(QStringLiteral("AntiAliasingCombo"));
    // in the order of View3DInventorViewer::AntiAliasing, which the
    // setting's number is
    aliasingCombo->addItems({tr("None"), tr("Line smoothing"), tr("MSAA 2x"), tr("MSAA 4x"),
                             tr("MSAA 8x")});
    aliasingCombo->setToolTip(doc(ViewParams::docAntiAliasing()));
    layout->addWidget(new QLabel(tr("Anti-aliasing:"), this), 1, 0);
    layout->addWidget(aliasingCombo, 1, 1);
    connect(aliasingCombo, qOverload<int>(&QComboBox::activated), this, [](int index) {
        if (ViewParams::getAntiAliasing() != index)
            ViewParams::setAntiAliasing(index);
    });
    layout->setColumnStretch(1, 1);
}

void DrawStyleOptionsWidget::refresh()
{
    auto viewer = viewerInFront();
    styleCombo->setEnabled(viewer != nullptr);
    if (viewer) {
        const std::string mode = viewer->getOverrideMode();
        int index = -1;
        int i = 0;
        for (auto cmd : styleCommands()) {
            // a style is set by its untranslated name (StdCmdDrawStyleBase)
            if (mode == cmd->getMenuText())
                index = i;
            ++i;
        }
        QSignalBlocker block(styleCombo);
        styleCombo->setCurrentIndex(index);
    }
    QSignalBlocker block(aliasingCombo);
    const long aliasing = ViewParams::getAntiAliasing();
    aliasingCombo->setCurrentIndex(aliasing >= 0 && aliasing < aliasingCombo->count()
                                       ? int(aliasing) : -1);
}

void DrawStyleOptionsWidget::install(QMenu *menu)
{
    if (!menu)
        return;
    auto widget = menu->findChild<DrawStyleOptionsWidget*>();
    if (!widget) {
        // The rows ActionGroup made, one a style: all there is in the menu
        // when this runs first. They stay in it, hidden -- they are the
        // commands' own actions.
        const QList<QAction*> rows = menu->actions();
        for (QAction *row : rows) {
            row->setProperty(HiddenRow, true);
            row->setVisible(false);
        }
        auto action = new QWidgetAction(menu);
        widget = new DrawStyleOptionsWidget(menu);
        action->setDefaultWidget(widget);
        menu->insertAction(rows.isEmpty() ? nullptr : rows.first(), action);
    }
    else {
        // ActionGroup shows a row again when its command's state changes
        for (QAction *row : menu->actions()) {
            if (row->property(HiddenRow).toBool() && row->isVisible())
                row->setVisible(false);
        }
    }
    widget->refresh();
}

// ---------------------------------------------------------------------------

LightOptionsWidget::LightOptionsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(12, 4, 12, 6);
    layout->setHorizontalSpacing(8);
    layout->addWidget(sectionTitle(tr("Lights"), this), 0, 0, 1, 3);

    addRow(Headlight, 1, tr("Headlight"), "EnableHeadlight", "HeadlightColor",
           "HeadlightIntensity");
    rows[Headlight].enable->setToolTip(
        tr("The light that follows the camera. Its direction is turned in the view: "
           "see Direction below."));
    addRow(FillLight, 2, tr("Fill light"), "EnableFillLight", "FillLightColor",
           "FillLightIntensity");
    rows[FillLight].enable->setToolTip(
        tr("A second light that follows the camera, from the side, to lift what the "
           "headlight leaves dark."));
    addRow(Ambient, 3, tr("Ambient light"), nullptr, "AmbientLightColor",
           "AmbientLightIntensity");

    auto buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 2, 0, 0);
    direction = new QPushButton(tr("Direction"), this);
    direction->setObjectName(QStringLiteral("LightDirectionButton"));
    direction->setCheckable(true);
    direction->setToolTip(
        tr("Turn the headlight in the active 3D view: a handle comes up in the middle of "
           "the view, and dragging it turns the light. Press this again, or Escape in "
           "the view, to take the handle away."));
    buttons->addWidget(direction);
    applyAll = new QPushButton(tr("Apply all"), this);
    applyAll->setObjectName(QStringLiteral("LightApplyAll"));
    applyAll->setToolTip(
        tr("Give every other open 3D view the lights of the active view, as they are "
           "now. A change made here afterwards is the active view's alone again."));
    buttons->addWidget(applyAll);
    buttons->addStretch(1);
    save = new QPushButton(tr("Save as default"), this);
    save->setObjectName(QStringLiteral("LightSaveButton"));
    save->setToolTip(
        tr("Store the lights of the active view in the preferences: what a view with no "
           "lights of its own is lit by, a new one for one. Until this is pressed a "
           "change made here is the view's own, kept with its document."));
    buttons->addWidget(save);
    layout->addLayout(buttons, 4, 0, 1, 3);
    layout->setColumnStretch(2, 1);

    connect(direction, &QPushButton::clicked, this, [this](bool on) {
        if (auto viewer = activeViewer())
            viewer->setLightManipulator(on);
        // the view is under the menu: it cannot be dragged in, nor
        // seen, until the menu is gone
        if (on)
            dismiss(this);
    });
    connect(applyAll, &QPushButton::clicked, this, [this]() {
        if (auto viewer = activeViewer()) {
            const int reached = viewer->applyLightSettingsToAllViews();
            getMainWindow()->showMessage(
                tr("The lights of this view were given to %n other view(s).", nullptr, reached),
                4000);
        }
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        if (auto viewer = activeViewer()) {
            viewer->saveLightSettings();
            getMainWindow()->showMessage(
                tr("The lights of this view are the default now."), 4000);
        }
    });
}

View3DInventorViewer *LightOptionsWidget::activeViewer() const
{
    return viewerInFront();
}

void LightOptionsWidget::addRow(int which, int gridRow, const QString &name,
                                const char *enableKey, const char *colourKey,
                                const char *intensityKey)
{
    Row &row = rows[which];
    row.enableKey = enableKey;
    row.colourKey = colourKey;
    row.intensityKey = intensityKey;
    auto layout = static_cast<QGridLayout*>(this->layout());

    if (enableKey) {
        row.enable = new QCheckBox(name, this);
        row.enable->setObjectName(QStringLiteral("Light_") + QLatin1String(enableKey));
        layout->addWidget(row.enable, gridRow, 0);
        connect(row.enable, &QCheckBox::toggled, this, [this, which](bool on) {
            if (refreshing)
                return;
            App::PropertyBool value;
            value.setValue(on);
            if (auto viewer = activeViewer())
                viewer->setLightSetting(rows[which].enableKey, value);
            rows[which].intensity->setEnabled(on);
        });
    }
    else {
        layout->addWidget(new QLabel(name, this), gridRow, 0);
    }

    row.colour = new QToolButton(this);
    row.colour->setObjectName(QStringLiteral("Light_") + QLatin1String(colourKey));
    row.colour->setToolTip(tr("The colour of this light"));
    row.colour->setAutoRaise(true);
    layout->addWidget(row.colour, gridRow, 1);
    connect(row.colour, &QToolButton::clicked, this, [this, which]() { chooseColour(which); });

    row.intensity = new QSlider(Qt::Horizontal, this);
    row.intensity->setObjectName(QStringLiteral("Light_") + QLatin1String(intensityKey));
    row.intensity->setRange(0, 100);
    row.intensity->setMinimumWidth(120);
    row.intensity->setToolTip(tr("The intensity of this light"));
    layout->addWidget(row.intensity, gridRow, 2);
    connect(row.intensity, &QSlider::valueChanged, this, [this, which](int percent) {
        if (refreshing)
            return;
        App::PropertyFloat value;
        value.setValue(percent / 100.0);
        if (auto viewer = activeViewer())
            viewer->setLightSetting(rows[which].intensityKey, value);
    });
}

void LightOptionsWidget::showColour(Row &row, const QColor &colour)
{
    row.shown = colour;
    QPixmap swatch(28, 14);
    swatch.fill(colour);
    row.colour->setIcon(QIcon(swatch));
    row.colour->setIconSize(swatch.size());
}

void LightOptionsWidget::chooseColour(int which)
{
    // The menu holds a popup grab, and a dialog raised under one gets no
    // input: the menu goes first, and the dialog is asked for from the
    // event loop, when every popup is down (as ShadingOptionsWidget does
    // for its file dialog).
    const QColor current = rows[which].shown;
    const QByteArray key(rows[which].colourKey);
    dismiss(this);
    QMetaObject::invokeMethod(getMainWindow(), [current, key]() {
        QColorDialog::ColorDialogOptions options;
        if (DialogOptions::dontUseNativeColorDialog())
            options |= QColorDialog::DontUseNativeDialog;
        const QColor picked = QColorDialog::getColor(
            current, getMainWindow(), LightOptionsWidget::tr("Colour of the light"), options);
        if (!picked.isValid())
            return;
        App::PropertyColor value;
        App::Color colour;
        colour.setValue<QColor>(picked);
        value.setValue(colour);
        if (auto viewer = viewerInFront())
            viewer->setLightSetting(key.constData(), value);
    }, Qt::QueuedConnection);
}

void LightOptionsWidget::refresh()
{
    auto viewer = activeViewer();
    Base::StateLocker guard(refreshing);
    setEnabled(viewer != nullptr);
    direction->setChecked(viewer && viewer->hasLightManipulator());
    if (!viewer)
        return;
    for (Row &row : rows) {
        bool on = true;
        if (row.enable) {
            App::PropertyBool value;
            if (viewer->getLightSetting(row.enableKey, value))
                on = value.getValue();
            row.enable->setChecked(on);
        }
        App::PropertyColor colour;
        if (viewer->getLightSetting(row.colourKey, colour))
            showColour(row, colour.getValue().asValue<QColor>());
        App::PropertyFloat intensity;
        if (viewer->getLightSetting(row.intensityKey, intensity))
            row.intensity->setValue(int(std::lround(intensity.getValue() * 100.0)));
        row.intensity->setEnabled(on);
    }
    // the headlight's direction is turned only where there is a headlight
    direction->setEnabled(rows[Headlight].enable->isChecked() || direction->isChecked());
}

void LightOptionsWidget::install(QMenu *menu)
{
    if (!menu)
        return;
    auto widget = menu->findChild<LightOptionsWidget*>();
    if (!widget) {
        menu->addSeparator();
        auto action = new QWidgetAction(menu);
        widget = new LightOptionsWidget(menu);
        action->setDefaultWidget(widget);
        menu->addAction(action);
    }
    widget->refresh();
}

#include "moc_DisplayOptions.cpp"
