// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Mod/PartDesign/App/FeaturePattern.h>

#include "ViewProviderPatterns.h"
#include "TaskPatternParameters.h"

using namespace PartDesignGui;

namespace
{
const char* Pixmaps[] = {"PartDesign_LinearPattern.svg",
                         "PartDesign_PolarPattern.svg",
                         "PartDesign_CircularPattern.svg",
                         "PartDesign_PathPattern.svg",
                         "PartDesign_PointPattern.svg"};
}  // namespace

PROPERTY_SOURCE(PartDesignGui::ViewProviderPattern, PartDesignGui::ViewProviderTransformed)

ViewProviderPattern::ViewProviderPattern()
{
    sPixmap = Pixmaps[0];
}

PartDesign::PatternFeature* ViewProviderPattern::getPattern() const
{
    return Base::freecad_dynamic_cast<PartDesign::PatternFeature>(getObject());
}

TaskDlgFeatureParameters *ViewProviderPattern::getEditDialog()
{
    return new TaskDlgPatternParameters(this);
}

QString ViewProviderPattern::getMenuName() const
{
    return tr("%1 parameters").arg(QString::fromLatin1(featureName().c_str()));
}

const std::string & ViewProviderPattern::featureName() const
{
    static const std::string names[] = {PartDesign::PatternFeature::KindLabels[0],
                                        PartDesign::PatternFeature::KindLabels[1],
                                        PartDesign::PatternFeature::KindLabels[2],
                                        PartDesign::PatternFeature::KindLabels[3],
                                        PartDesign::PatternFeature::KindLabels[4]};
    auto pattern = getPattern();
    return names[pattern ? static_cast<int>(pattern->getPatternType()) : 0];
}

void ViewProviderPattern::updatePixmap()
{
    if (auto pattern = getPattern()) {
        sPixmap = Pixmaps[static_cast<int>(pattern->getPatternType())];
    }
}

void ViewProviderPattern::attach(App::DocumentObject* obj)
{
    ViewProviderTransformed::attach(obj);
    updatePixmap();
}

void ViewProviderPattern::updateData(const App::Property* prop)
{
    auto pattern = getPattern();
    if (pattern && prop == &pattern->PatternType) {
        updatePixmap();
        signalChangeIcon();
    }
    ViewProviderTransformed::updateData(prop);
}

PROPERTY_SOURCE(PartDesignGui::ViewProviderLinearPattern, PartDesignGui::ViewProviderPattern)
PROPERTY_SOURCE(PartDesignGui::ViewProviderPolarPattern, PartDesignGui::ViewProviderPattern)
PROPERTY_SOURCE(PartDesignGui::ViewProviderCircularPattern, PartDesignGui::ViewProviderPattern)
PROPERTY_SOURCE(PartDesignGui::ViewProviderPathPattern, PartDesignGui::ViewProviderPattern)
PROPERTY_SOURCE(PartDesignGui::ViewProviderPointPattern, PartDesignGui::ViewProviderPattern)
