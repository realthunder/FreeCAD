/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_DIALOG_DLGMATERIALPROPERTIES_IMP_H
#define GUI_DIALOG_DLGMATERIALPROPERTIES_IMP_H

#include <QDialog>
#include <memory>
#include <utility>
#include <vector>

#include <FCGlobal.h>

namespace App {
class Property;
class PropertyAppearance;
class PropertyAppearanceList;
}

namespace Gui {
class ViewProvider;

namespace Dialog {
class Ui_DlgMaterialProperties;

/** The appearance editor
 *
 * Edits the named material property -- ShapeAppearance's whole list, or a
 * plain PropertyAppearance like a colour plot's TextureMaterial -- across
 * every given view provider. Every edit applies as it is committed (a
 * colour picked, a spin box stepped), so the 3D view answers live; OK
 * keeps the result and Cancel restores the appearance the dialog opened
 * on. On a material list the shading model can be toggled between Phong
 * and PBR, converting the stored values so the look survives the switch.
 */
class GuiExport DlgMaterialPropertiesImp : public QDialog
{
    Q_OBJECT

public:
    explicit DlgMaterialPropertiesImp(const std::string& mat, QWidget* parent = nullptr, Qt::WindowFlags fl = Qt::WindowFlags());
    ~DlgMaterialPropertiesImp() override;
    void setViewProviders(const std::vector<Gui::ViewProvider*>&);
    QColor diffuseColor() const;

    void reject() override;

private:
    void setupConnections();
    void onShadingModelActivated(int);
    void onAmbientColorChanged();
    void onDiffuseColorChanged();
    void onEmissiveColorChanged();
    void onSpecularColorChanged();
    void onShininessValueChanged(int);
    void onMetallicValueChanged(int);
    void onRoughnessValueChanged(int);

    App::PropertyAppearanceList* listProperty(Gui::ViewProvider*) const;
    App::PropertyAppearance* singleProperty(Gui::ViewProvider*) const;
    /// Relabel and show/hide the rows for the shading model
    void updateModeView(bool pbr);
    /// Refresh every control from the first object holding the property
    void syncFromProperty();

private:
    std::unique_ptr<Ui_DlgMaterialProperties> ui;
    std::string material;
    std::vector<Gui::ViewProvider*> Objects;
    /// What Cancel restores: the property as the dialog found it
    std::vector<std::pair<Gui::ViewProvider*, std::unique_ptr<App::Property>>> snapshots;
};

} // namespace Dialog
} // namespace Gui

#endif // GUI_DIALOG_DLGMATERIALPROPERTIES_IMP_H
