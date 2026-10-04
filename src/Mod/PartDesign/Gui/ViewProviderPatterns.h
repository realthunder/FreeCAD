// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef PARTGUI_ViewProviderPatterns_H
#define PARTGUI_ViewProviderPatterns_H

#include "ViewProviderTransformed.h"

namespace PartDesign
{
class PatternFeature;
}

namespace PartDesignGui {

/// The view provider of a pattern of any kind, edited in the pattern panel.
/// Its icon follows the kind.
class PartDesignGuiExport ViewProviderPattern : public ViewProviderTransformed
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderPattern)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderPattern);

public:
    ViewProviderPattern();

    QString getMenuName() const override;
    const std::string & featureName() const override;
    void attach(App::DocumentObject* obj) override;
    void updateData(const App::Property* prop) override;

protected:
    TaskDlgFeatureParameters *getEditDialog() override;

private:
    PartDesign::PatternFeature* getPattern() const;
    void updatePixmap();
};

/// The view providers the pattern classes had, kept for a file that names
/// one as a custom view type
#define PARTDESIGN_PATTERN_VIEWPROVIDER(_kind_)                                    \
    class PartDesignGuiExport ViewProvider##_kind_##Pattern : public ViewProviderPattern \
    {                                                                              \
        PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProvider##_kind_##Pattern); \
    };

PARTDESIGN_PATTERN_VIEWPROVIDER(Linear)
PARTDESIGN_PATTERN_VIEWPROVIDER(Polar)
PARTDESIGN_PATTERN_VIEWPROVIDER(Circular)
PARTDESIGN_PATTERN_VIEWPROVIDER(Path)
PARTDESIGN_PATTERN_VIEWPROVIDER(Point)

} // namespace PartDesignGui


#endif // PARTGUI_ViewProviderPatterns_H
