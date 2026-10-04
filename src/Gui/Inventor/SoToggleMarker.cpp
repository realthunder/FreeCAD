// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
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
#include <algorithm>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <Inventor/misc/SoNotification.h>
#endif

#include <Base/Tools.h>

#include "BitmapFactory.h"
#include "SoToggleMarker.h"

using namespace Gui;

SO_NODE_SOURCE(SoToggleMarker)

void SoToggleMarker::initClass()
{
    SO_NODE_INIT_CLASS(SoToggleMarker, SoImage, "Image");
}

SoToggleMarker::SoToggleMarker()
{
    SO_NODE_CONSTRUCTOR(SoToggleMarker);
    SO_NODE_ADD_FIELD(active, (TRUE));
    SO_NODE_ADD_FIELD(highlighted, (FALSE));
    SO_NODE_ADD_FIELD(markerSize, (22));
    // Centred on its point, where an SoImage hangs off it to the upper right
    horAlignment = SoImage::CENTER;
    vertAlignment = SoImage::HALF;
    paint();
}

void SoToggleMarker::notify(SoNotList* list)
{
    // Painted now, not at the next GL pass: the render cache reads the image
    // field before any method of this node runs, and with a backend drawing
    // there may be no GL pass at all
    SoField* f = list->getLastField();
    if (!painting && (f == &active || f == &highlighted || f == &markerSize)) {
        paint();
    }
    inherited::notify(list);
}

void SoToggleMarker::paint()
{
    Base::StateLocker guard(painting);

    const int size = std::clamp(markerSize.getValue(), 8, 256);
    // Drawn on a grid of 18, upstream's icon, inside a ring of 24
    const qreal unit = size / 24.0;
    const bool in = active.getValue();
    const bool hot = highlighted.getValue();

    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);

    // The disc. What is out is fainter, as the instance it stands for is
    // gone from the view; under the pointer it is opaque and ringed in the
    // colour of a highlight.
    QColor fill(255, 255, 255, hot ? 250 : (in ? 215 : 160));
    QPen ring = hot ? QPen(QColor(0, 120, 215), 2.0 * unit) : QPen(QColor(40, 40, 40, 120), unit);
    painter.setPen(ring);
    painter.setBrush(fill);
    const qreal inset = ring.widthF() / 2.0 + 0.5;
    painter.drawEllipse(QRectF(inset, inset, size - 2 * inset, size - 2 * inset));

    // The glyph: what a click does
    const qreal c = size / 2.0;
    const qreal arm = 4.5 * unit;
    QColor colour = in ? QColor(208, 36, 36) : QColor(29, 132, 72);
    painter.setPen(QPen(colour, 3.0 * unit, Qt::SolidLine, Qt::RoundCap));
    if (in) {
        painter.drawLine(QPointF(c - arm, c - arm), QPointF(c + arm, c + arm));
        painter.drawLine(QPointF(c + arm, c - arm), QPointF(c - arm, c + arm));
    }
    else {
        painter.drawLine(QPointF(c, c - arm - unit), QPointF(c, c + arm + unit));
        painter.drawLine(QPointF(c - arm - unit, c), QPointF(c + arm + unit, c));
    }
    painter.end();

    SoSFImage sfimage;
    BitmapFactory().convert(img, sfimage);
    SbVec2s dim;
    int nc = 0;
    const unsigned char* bytes = sfimage.getValue(dim, nc);
    image.setValue(dim, nc, bytes);
}
