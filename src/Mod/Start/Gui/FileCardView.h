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

#ifndef FREECAD_START_FILECARDVIEW_H
#define FREECAD_START_FILECARDVIEW_H

#include <QListView>
#include <QPersistentModelIndex>

namespace StartGui
{

class FileCardView: public QListView
{
    Q_OBJECT

public:
    explicit FileCardView(QWidget* parent = nullptr);

    int heightForWidth(int width) const override;

    QSize sizeHint() const override;

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// Repaint every card when the hovered one changes, rather than leave it
    /// to the view's own two-rect update. A card's highlight is drawn at the
    /// very edge of its item rect, and the rect the view repaints on a hover
    /// change does not reach that far, so the card being left keeps its
    /// border while the new one gains its own -- two cards lit at once.
    void refreshHover();

    QPersistentModelIndex _hovered;
};

}  // namespace StartGui

#endif  // FREECAD_START_FILECARDVIEW_H
