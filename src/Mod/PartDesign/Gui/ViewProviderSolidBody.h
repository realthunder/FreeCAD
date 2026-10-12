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

#ifndef PARTGUI_ViewProviderSolidBody_H
#define PARTGUI_ViewProviderSolidBody_H

#include <QByteArray>
#include <QCoreApplication>

#include <Mod/Part/Gui/ViewProvider.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

namespace PartDesign {
class Body;
}

namespace PartDesignGui {

/** @name The tip icons of the tree
 *
 * A body that keeps solid bodies (Body::AddSolidBody) marks its tips with
 * arrows in the tree: one on the feature each solid is rolled back to, and
 * one of another shape and colour on the solid body that is the body's tip.
 * Either can be dragged onto another item to move the tip there. A body that
 * does not, marks its tip with the tag icon, which is dragged the same way.
 */
//@{
/// Tag of the arrow on the feature that is the tip of its solid
PartDesignGuiExport const QByteArray &solidTipIconTag();
/// Tag of the arrow on the solid body that is the tip of the body
PartDesignGuiExport const QByteArray &bodyTipIconTag();
/// Tag of the tag icon of a feature, which marks the tip of a body without solid bodies
PartDesignGuiExport const QByteArray &featureTagIconTag();

/// Whether the tip icon dragged off \a source moves a tip when dropped on \a target
PartDesignGuiExport bool canDropTipIcon(App::DocumentObject *source,
                                        const QByteArray &iconTag,
                                        App::DocumentObject *target);
/// Move the tip the icon dragged off \a source stands for to \a target
PartDesignGuiExport void dropTipIcon(App::DocumentObject *source,
                                     const QByteArray &iconTag,
                                     App::DocumentObject *target);
/// Whether \a feature is where its solid is rolled back to. Without a solid
/// body, the solid of the body's tip is rolled back to that tip.
PartDesignGuiExport bool isSolidTip(const PartDesign::Body *body,
                                    const App::DocumentObject *feature);
//@}

/** View provider of PartDesign::SolidBody
 *
 * In the tree the solid body holds the features of its solid. In the 3D view
 * the features draw the solid; the solid body's own shape is hidden unless
 * asked for, and is what Body.SolidBody.Face1 is picked on.
 */
class PartDesignGuiExport ViewProviderSolidBody : public PartGui::ViewProviderPart
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderSolidBody)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderSolidBody);
    using inherited = PartGui::ViewProviderPart;

public:
    ViewProviderSolidBody();
    ~ViewProviderSolidBody() override;

    void updateData(const App::Property *prop) override;
    std::vector<App::DocumentObject*> claimChildren() const override;

    void getExtraIcons(std::vector<std::pair<QByteArray, QPixmap> > &icons) const override;
    QString getToolTip(const QByteArray &iconTag) const override;
    bool canDragIcon(const QByteArray &iconTag) const override;
    bool canDropIcon(App::DocumentObject *source, const QByteArray &iconTag) const override;
    void dropIcon(App::DocumentObject *source, const QByteArray &iconTag) override;

    // What is under a solid body is a feature of the body: the body moves it
    bool canDragObjects() const override {return false;}
    bool canDragObject(App::DocumentObject*) const override {return false;}
    bool canDropObjects() const override {return false;}
    bool canDropObject(App::DocumentObject*) const override {return false;}
    bool canReplaceObject(App::DocumentObject *, App::DocumentObject *) override;
    int replaceObject(App::DocumentObject *, App::DocumentObject *) override;
    bool canReorderObject(App::DocumentObject *, App::DocumentObject *) override;
    bool reorderObjects(const std::vector<App::DocumentObject *> &, App::DocumentObject *) override;

    bool canDelete(App::DocumentObject *obj) const override;
    /// Deleting a solid body deletes the solid: every feature it holds
    bool onDelete(const std::vector<std::string> &) override;

protected:
    void setupContextMenu(QMenu *menu, QObject *receiver, const char *member) override;
    bool setEdit(int ModNum) override;
};

} // namespace PartDesignGui

#endif // PARTGUI_ViewProviderSolidBody_H
