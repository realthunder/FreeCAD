// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef PARTGUI_ViewProviderPatterns_H
#define PARTGUI_ViewProviderPatterns_H

#include "ViewProviderTransformed.h"

namespace PartDesignGui {

/// The view providers of the circular, path and point patterns, edited in
/// the pattern panel as the linear and polar ones are
class PartDesignGuiExport ViewProviderCircularPattern : public ViewProviderTransformed
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderCircularPattern)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderCircularPattern);
public:
    ViewProviderCircularPattern() {
        sPixmap = "PartDesign_CircularPattern.svg";
    }

    QString getMenuName() const override {
        return tr("CircularPattern parameters");
    }

    const std::string & featureName() const override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;

protected:
    TaskDlgFeatureParameters *getEditDialog() override;
};

class PartDesignGuiExport ViewProviderPathPattern : public ViewProviderTransformed
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderPathPattern)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderPathPattern);
public:
    ViewProviderPathPattern() {
        sPixmap = "PartDesign_PathPattern.svg";
    }

    QString getMenuName() const override {
        return tr("PathPattern parameters");
    }

    const std::string & featureName() const override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;

protected:
    TaskDlgFeatureParameters *getEditDialog() override;
};

class PartDesignGuiExport ViewProviderPointPattern : public ViewProviderTransformed
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderPointPattern)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderPointPattern);
public:
    ViewProviderPointPattern() {
        sPixmap = "PartDesign_PointPattern.svg";
    }

    QString getMenuName() const override {
        return tr("PointPattern parameters");
    }

    const std::string & featureName() const override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;

protected:
    TaskDlgFeatureParameters *getEditDialog() override;
};

} // namespace PartDesignGui

#endif // PARTGUI_ViewProviderPatterns_H
