/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef GUI_VIEWPROVIDER_SAVED_VIEW_H
#define GUI_VIEWPROVIDER_SAVED_VIEW_H

#include <QFlags>
#include "ViewProviderDocumentObject.h"

namespace App {
class SavedView;
}

namespace Gui {

class GuiExport ViewProviderSavedView : public ViewProviderDocumentObject {
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderDocumentObject);
    typedef ViewProviderDocumentObject inherited;
public:
    ViewProviderSavedView();
    virtual ~ViewProviderSavedView();

    void setupContextMenu(QMenu*, QObject*, const char*) override;
    PyObject *getPyObject() override;
    bool doubleClicked() override;
    void getExtraIcons(std::vector<std::pair<QByteArray, QPixmap> > &) const override;
    bool iconMouseEvent(QMouseEvent *, const QByteArray &tag) override;
    QString getToolTip(const QByteArray &tag) const override;
    void finishRestoring() override;

    enum CaptureOption {
        Clippings       = 0x01,
        Camera          = 0x02,
        Visibilities    = 0x04,
        DrawStyle       = 0x08,
        ShowOnTop       = 0x10,
        RenderSettings  = 0x20,
        Default         = 0x80,
        All             = Clippings | Camera | Visibilities | ShowOnTop | DrawStyle
                          | RenderSettings,
    };
    Q_DECLARE_FLAGS(CaptureOptions, CaptureOption);

    void capture(CaptureOptions options = Default);
    void apply(CaptureOptions options = Default);
    void checkOptions(App::SavedView *obj, CaptureOptions &options) const;

protected:
    bool setEdit(int ModNum) override;
    void prepareMenu(QMenu *);
};

} // namespace Gui

Q_DECLARE_OPERATORS_FOR_FLAGS(Gui::ViewProviderSavedView::CaptureOptions);

#endif //GUI_VIEWPROVIDER_SAVED_VIEW_H
