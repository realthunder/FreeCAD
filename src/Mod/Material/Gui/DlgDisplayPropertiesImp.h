// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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


#pragma once

#include <QDialog>
#include <memory>
#include <vector>

#include <App/MaterialAppearance.h>
#include <Gui/Selection/Selection.h>
#include <Gui/TaskView/TaskDialog.h>
#include <Gui/TaskView/TaskView.h>

#include <Mod/Material/App/Materials.h>

namespace App
{
class Property;
}

namespace MatGui
{

class ViewProvider;
class Command;

/**
 * The DlgDisplayPropertiesImp class implements a dialog containing all available document
 * templates to create a new document.
 * \author Jürgen Riegel
 */
class DlgDisplayPropertiesImp: public QDialog, public Gui::SelectionSingleton::ObserverType
{
    Q_OBJECT

public:
    explicit DlgDisplayPropertiesImp(QWidget* parent = nullptr,
                                     Qt::WindowFlags fl = Qt::WindowFlags());
    ~DlgDisplayPropertiesImp() override;
    /// Observer message from the Selection
    void OnChange(Gui::SelectionSingleton::SubjectType& rCaller,
                  Gui::SelectionSingleton::MessageType Reason) override;
    void showDefaultButtons(bool);
    void reject() override;
    /// Offer the whole preset list when the finish box is clicked into,
    /// which is what a combo box did for free.
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// Write the rest of the matching preset name after what was typed,
    /// selected, so it is accepted by Enter and replaced by typing on.
    void inlineCompleteFinish(const QString& typed);

private Q_SLOTS:
    void onChangeModeActivated(const QString&);
    void onChangePlotActivated(const QString&);
    void onSpinTransparencyValueChanged(int);
    void onSpinPointSizeValueChanged(double);
    void onButtonColorChanged();
    void onButtonLineColorChanged();
    void onButtonPointColorChanged();
    void onSpinLineWidthValueChanged(double);
    void onSpinLineTransparencyValueChanged(int);
    void onButtonCustomAppearanceClicked();
    void onButtonColorPlotClicked();
    void onMaterialSelected(const std::shared_ptr<Materials::Material>& material);
    /// The picker at the top: the object's material CARD, not its look
    void onCardSelected(const std::shared_ptr<Materials::Material>& material);
    /// Take the card's look again, and follow it from now on
    void onResetToMaterial();
    void onEditShaderGraph();
    void onFinishPresetActivated(const QString&);
    void onFinishSizeChanged(double);
    void onMapFaceColorChanged(bool);
    void onMapLineColorChanged(bool);
    void onMapPointColorChanged(bool);
    void onMapTransparencyChanged(bool);

protected:
    void changeEvent(QEvent* e) override;

private:
    void setupConnections();
    void setupFilters();
    void slotChangedObject(const Gui::ViewProvider&, const App::Property& Prop);
    void slotDeletedObject(const Gui::ViewProvider&);
    void slotDeleteDocument(const Gui::Document&);
    void setDisplayModes(const std::vector<Gui::ViewProvider*>&);
    void setColorPlot(const std::vector<Gui::ViewProvider*>&);
    void setShapeAppearance(const std::vector<Gui::ViewProvider*>&);
    /** The card picker, and the line that says where this look came from
     *
     * docs/MaterialStorage.md 15.5. The picker is there only while the
     * selection carries a material card, because without one there is
     * nothing for the look to follow; the line reads "As material Steel",
     * "Custom" or "Custom, 3 faces painted".
     */
    void setMaterialCard(const std::vector<Gui::ViewProvider*>&);
    /// Fill the finish combo with one row per pattern, on the icons the
    /// Material module renders for them
    void setupFinishPresets();
    void setShapeFinish(const std::vector<Gui::ViewProvider*>&);
    /// Write the row's finish onto every entry of each target's appearance
    void applyFinish();
    void setShapeColor(const std::vector<Gui::ViewProvider*>&);
    void setLineColor(const std::vector<Gui::ViewProvider*>&);
    void setPointColor(const std::vector<Gui::ViewProvider*>&);
    void setPointSize(const std::vector<Gui::ViewProvider*>&);
    void setLineWidth(const std::vector<Gui::ViewProvider*>&);
    void setTransparency(const std::vector<Gui::ViewProvider*>&);
    void setLineTransparency(const std::vector<Gui::ViewProvider*>&);
    void setMapFaceColor(const std::vector<Gui::ViewProvider*>&);
    void setMapEdgeColor(const std::vector<Gui::ViewProvider*>&);
    void setMapVertexColor(const std::vector<Gui::ViewProvider*>&);
    void setMapTransparency(const std::vector<Gui::ViewProvider*>&);
    void onPropertyBoolChanged(const char* name, bool checked);
    std::vector<Gui::ViewProvider*> getSelection() const;
    /// Adopt a selection as what the dialog edits from now on
    void rememberTargets(const std::vector<Gui::ViewProvider*>& views);
    /// The objects the dialog edits: the last selection it saw, which outlives
    /// the selection itself. Resolved from names on every call, so an object
    /// that has been deleted drops out instead of dangling.
    std::vector<Gui::ViewProvider*> getTargets() const;
    void setPropertiesFromSelection();

private:
    class Private;
    std::unique_ptr<Private> d;
};

class TaskDisplayProperties: public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    TaskDisplayProperties();
    ~TaskDisplayProperties() override;

public:
    bool reject() override;

    bool isAllowedAlterDocument() const override
    {
        return true;
    }
    bool isAllowedAlterView() const override
    {
        return true;
    }
    bool isAllowedAlterSelection() const override
    {
        return true;
    }
    QDialogButtonBox::StandardButtons getStandardButtons() const override;

private:
    DlgDisplayPropertiesImp* widget;
};

}  // namespace MatGui