/***************************************************************************
 *   Copyright (c) 2024 Pierre-Louis Boyer <development@ondsel.com>        *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <QGuiApplication>
#include <QPainter>
#include <QWidget>
#endif

#include <App/Application.h>
#include <Base/Parameter.h>

#include "BitmapFactory.h"
#include "InputHint.h"
#include "MainWindow.h"
#include "ToolHandler.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewerContext.h"

using namespace Gui;

/**************************** ToolHandler *******************************************/

QString ToolHandler::getCrosshairCursorSVGName() const
{
    return QStringLiteral("None");
}

bool ToolHandler::activate()
{
    Gui::ViewerContext* viewer = getViewer();
    if (!viewer) {
        return false;
    }

    // Save the cursor at the time the tool is activated. A view with no
    // widget has none, and that is not a reason to refuse the tool -- the
    // cursor is chrome, and the DOM layer's (docs/ThinClient.md sec 8.7).
    if (QWidget* widget = viewer->getWidget()) {
        oldCursor = widget->cursor();
    }

    updateCursor();
    updateHint();

    this->preActivated();
    this->activated();
    return true;
}

void ToolHandler::deactivate()
{
    this->deactivated();
    this->postDeactivated();

    unsetCursor();

    Gui::getMainWindow()->hideHints();
}

//**************************************************************************
// Helpers

unsigned long ToolHandler::getCrosshairColor()
{
    unsigned long color = 0xFFFFFFFF;  // white
    ParameterGrp::handle hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    color = hGrp->GetUnsigned("CursorCrosshairColor", color);
    // from rgba to rgb
    color = (color >> 8) & 0xFFFFFF;
    return color;
}

void ToolHandler::setCrosshairCursor(const QString& svgName)
{
    const unsigned long defaultCrosshairColor = 0xFFFFFF;
    unsigned long color = getCrosshairColor();
    auto colorMapping = std::map<unsigned long, unsigned long>();
    colorMapping[defaultCrosshairColor] = color;
    // hot spot of all SVG icons should be 8,8 for 32x32 size (16x16 for 64x64)
    int hotX = 8;
    int hotY = 8;
    setSvgCursor(svgName, hotX, hotY, colorMapping);
}

void ToolHandler::setCrosshairCursor(const char* svgName)
{
    QString cursorName = QString::fromUtf8(svgName);
    setCrosshairCursor(cursorName);
}

void ToolHandler::setSvgCursor(const QString& cursorName,
                               int x,
                               int y,
                               const std::map<unsigned long, unsigned long>& colorMapping)
{
    // The Sketcher_Pointer_*.svg icons have a default size of 64x64. When directly creating
    // them with a size of 32x32 they look very bad.
    // As a workaround the icons are created with 64x64 and afterwards the pixmap is scaled to
    // 32x32. This workaround is only needed if pRatio is equal to 1.0
    //
    qreal pRatio = devicePixelRatio();
    bool isRatioOne = (pRatio == 1.0);
    qreal defaultCursorSize = isRatioOne ? 64 : 32;
    qreal hotX = x;
    qreal hotY = y;
#if !defined(Q_OS_WIN32) && !defined(Q_OS_MAC)
    if (qGuiApp->platformName() == QStringLiteral("xcb")) {
        hotX *= pRatio;
        hotY *= pRatio;
    }
#endif
    qreal cursorSize = defaultCursorSize * pRatio;

    QPixmap pointer = Gui::BitmapFactory().pixmapFromSvg(cursorName.toStdString().c_str(),
                                                         QSizeF(cursorSize, cursorSize),
                                                         colorMapping);
    if (isRatioOne) {
        pointer = pointer.scaled(32, 32);
    }
    pointer.setDevicePixelRatio(pRatio);
    setCursor(pointer, hotX, hotY, false);
}

void ToolHandler::setCursor(const QPixmap& p, int x, int y, bool autoScale)
{
    // A desktop view: this ends in QWidget::setCursor, and a mirror has
    // no widget to set one on.
    Gui::View3DInventorViewer* viewer = getDesktopViewer();
    if (viewer) {
        QCursor cursor;
        QPixmap p1(p);
        // TODO remove autoScale after all cursors are SVG-based
        if (autoScale) {
            qreal pRatio = viewer->devicePixelRatio();
            int newWidth = p.width() * pRatio;
            int newHeight = p.height() * pRatio;
            p1 = p1.scaled(newWidth, newHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p1.setDevicePixelRatio(pRatio);
            qreal hotX = x;
            qreal hotY = y;
#if !defined(Q_OS_WIN32) && !defined(Q_OS_MAC)
            if (qGuiApp->platformName() == QStringLiteral("xcb")) {
                hotX *= pRatio;
                hotY *= pRatio;
            }
#endif
            cursor = QCursor(p1, hotX, hotY);
        }
        else {
            // already scaled
            cursor = QCursor(p1, x, y);
        }

        actCursor = cursor;
        actCursorPixmap = p1;

        viewer->getWidget()->setCursor(cursor);
    }
}

void ToolHandler::addCursorTail(std::vector<QPixmap>& pixmaps)
{
    // Create a pixmap that will contain icon and each autoconstraint icon
    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
        QPixmap baseIcon = QPixmap(actCursorPixmap);
        baseIcon.setDevicePixelRatio(actCursorPixmap.devicePixelRatio());
        qreal pixelRatio = baseIcon.devicePixelRatio();
        // cursor size in device independent pixels
        qreal baseCursorWidth = baseIcon.width();
        qreal baseCursorHeight = baseIcon.height();

        int tailWidth = 0;
        for (auto const& p : pixmaps) {
            tailWidth += p.width();
        }

        int newIconWidth = baseCursorWidth + tailWidth;
        int newIconHeight = baseCursorHeight;

        QPixmap newIcon(newIconWidth, newIconHeight);
        newIcon.fill(Qt::transparent);

        QPainter qp;
        qp.begin(&newIcon);

        qp.drawPixmap(QPointF(0, 0),
                      baseIcon.scaled(baseCursorWidth * pixelRatio,
                                      baseCursorHeight * pixelRatio,
                                      Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));

        // Iterate through pixmaps and them to the cursor pixmap
        std::vector<QPixmap>::iterator pit = pixmaps.begin();
        int i = 0;
        qreal currentIconX = baseCursorWidth;
        qreal currentIconY;

        for (; pit != pixmaps.end(); ++pit, i++) {
            QPixmap icon = *pit;
            currentIconY = baseCursorHeight - icon.height();
            qp.drawPixmap(QPointF(currentIconX, currentIconY), icon);
            currentIconX += icon.width();
        }

        qp.end();  // Finish painting

        // Create the new cursor with the icon.
        QPoint p = actCursor.hotSpot();
        newIcon.setDevicePixelRatio(pixelRatio);
        QCursor newCursor(newIcon, p.x(), p.y());
        applyCursor(newCursor);
    }
}

void ToolHandler::updateCursor()
{
    auto cursorstring = getCrosshairCursorSVGName();

    if (cursorstring != QStringLiteral("None")) {
        setCrosshairCursor(cursorstring);
    }
}

std::list<InputHint> ToolHandler::getToolHints() const
{
    return {};
}

void ToolHandler::updateHint() const
{
    Gui::getMainWindow()->showHints(getToolHints());
}

void ToolHandler::applyCursor()
{
    applyCursor(actCursor);
}

void ToolHandler::applyCursor(QCursor& newCursor)
{
    Gui::View3DInventorViewer* viewer = getDesktopViewer();
    if (viewer) {
        viewer->getWidget()->setCursor(newCursor);
    }
}

void ToolHandler::unsetCursor()
{
    Gui::View3DInventorViewer* viewer = getDesktopViewer();
    if (viewer) {
        viewer->getWidget()->setCursor(oldCursor);
    }
}

qreal ToolHandler::devicePixelRatio()
{
    // The client's, not a screen's: a mirror answers this from what the
    // browser stated over the wire.
    qreal pixelRatio = 1;
    Gui::ViewerContext* viewer = getViewer();
    if (viewer) {
        pixelRatio = viewer->devicePixelRatio();
    }
    return pixelRatio;
}

Gui::ViewerContext* ToolHandler::getViewer()
{
    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
        return static_cast<Gui::View3DInventor*>(view)->getViewer();
    }
    return nullptr;
}

Gui::View3DInventorViewer* ToolHandler::getDesktopViewer()
{
    return dynamic_cast<Gui::View3DInventorViewer*>(getViewer());
}
