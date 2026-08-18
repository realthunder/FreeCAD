/***************************************************************************
 *   Copyright (c) 2013 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_DIALOG_CLIPPING_H
#define GUI_DIALOG_CLIPPING_H

#include <QDialog>
#include <FCGlobal.h>
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/fields/SoSFPlane.h>
#include <Inventor/fields/SoSFFloat.h>
#include <Base/Placement.h>
#include "InventorBase.h"
#include "SoFCCSysDragger.h"

class QDockWidget;

class SoDrawStyle;
class SoClipPlane;
class SoBaseColor;
class SoCoordinate3;

class ClipDragger : public Gui::SoFCCSysDragger {
    typedef SoFCCSysDragger inherited;

    SO_NODE_HEADER(ClipDragger);

public:
    SoSFFloat planeSize;
    SoSFFloat lineWidth;
    SoSFColor planeColor;
    SoSFPlane plane;
    std::function<void(ClipDragger*)> dragDone;

    static void initClass(void);

    ClipDragger(SoClipPlane *clip = nullptr, bool custom = false);
    ~ClipDragger();

    virtual void notify(SoNotList * l) override;

    void init(SoClipPlane *clip, bool custom = false);

    SoClipPlane *getClipPlane() const {return clip;}

    void onDragStart();
    void onDragFinish();
    void onDragMotion();
    void updateSize();

private:
    Gui::CoinPtr<SoCoordinate3> coords;
    Gui::CoinPtr<SoClipPlane> clip;
    Gui::CoinPtr<SoBaseColor> color;
    Gui::CoinPtr<SoDrawStyle> drawStyle;
    bool busy = false;
};

namespace Gui {
class View3DInventor;
namespace Dialog {

/**
 * @author Werner Mayer
 */
class GuiExport Clipping : public QDialog
{
    Q_OBJECT

public:
    Clipping(Gui::View3DInventor* view, QWidget* parent = nullptr);
    ~Clipping() override;

    static void restoreClipPlanes(Gui::View3DInventor *view,
                                  const Base::Vector3d &posX, bool enableX,
                                  const Base::Vector3d &posY, bool enableY,
                                  const Base::Vector3d &posZ, bool enableZ,
                                  const Base::Placement &plaCustom, bool enableCustom);

    static void getClipPlanes(Gui::View3DInventor *view,
                              Base::Vector3d &posX, bool &enableX,
                              Base::Vector3d &posY, bool &enableY,
                              Base::Vector3d &posZ, bool &enableZ,
                              Base::Placement &plaCustom, bool &enableCustom);

    static void toggle(View3DInventor *view = nullptr);

    /** Whether a view shows the clip plane dragger
     *
     * The view's own Section_ShowPlane override where it carries one, and
     * this installation's preference where it does not.
     */
    static bool showPlane(Gui::View3DInventor *view);
    /// Give a view its own answer for that, which is what travels with it.
    static void setShowPlane(Gui::View3DInventor *view, bool on);

    /** Follow a style property a view was just given
     *
     * A Section_ or Light_ property can be set by anything -- a saved view
     * applied, a script, this dialog -- and a panel open on that view is
     * showing what it says, so it follows.
     */
    static void onViewPropertyChanged(Gui::View3DInventor *view, const char *property);

    virtual bool eventFilter(QObject *, QEvent*);

protected Q_SLOTS:
    void onViewDestroyed(QObject *);
    void on_angleX_valueChanged(double);
    void on_angleY_valueChanged(double);
    void on_checkBoxFill_toggled(bool);
    void on_checkBoxInvert_toggled(bool);
    void on_checkBoxConcave_toggled(bool);
    void on_checkBoxOnTop_toggled(bool);
    void on_checkBoxShowPlane_toggled(bool);
    void on_spinBoxHatchScale_valueChanged(double);
    void on_checkBoxHatch_toggled(bool on);
    void on_editHatchTexture_fileNameSelected(const QString &filename);

protected:
    void setupConnections();
    void onGroupBoxXToggled(bool);
    void onGroupBoxYToggled(bool);
    void onGroupBoxZToggled(bool);
    void onClipXValueChanged(double);
    void onClipYValueChanged(double);
    void onClipZValueChanged(double);
    void onFlipClipXClicked();
    void onFlipClipYClicked();
    void onFlipClipZClicked();
    void onGroupBoxViewToggled(bool);
    void onClipViewValueChanged(double);
    void onFromViewClicked();
    void onAdjustViewdirectionToggled(bool);
    void onDirXValueChanged(double);
    void onDirYValueChanged(double);
    void onDirZValueChanged(double);

public:
    void done(int) override;

private:
    /// Close this view's panel, leaving every other view's standing.
    void closePanel();
    /// The stack page (a QScrollArea) this panel sits in, if any.
    QWidget *stackPage() const;

    class Private;
    Private* d;
};

} // namespace Dialog
} // namespace Gui

#endif // GUI_DIALOG_CLIPPING_H
