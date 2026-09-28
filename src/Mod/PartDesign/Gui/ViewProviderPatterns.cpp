// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "ViewProviderPatterns.h"
#include "TaskPatternParameters.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderCircularPattern, PartDesignGui::ViewProviderTransformed)

TaskDlgFeatureParameters *ViewProviderCircularPattern::getEditDialog()
{
    return new TaskDlgPatternParameters(this);
}

void ViewProviderCircularPattern::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit circular pattern"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

const std::string & ViewProviderCircularPattern::featureName() const
{
    static const std::string name = "CircularPattern";
    return name;
}

PROPERTY_SOURCE(PartDesignGui::ViewProviderPathPattern, PartDesignGui::ViewProviderTransformed)

TaskDlgFeatureParameters *ViewProviderPathPattern::getEditDialog()
{
    return new TaskDlgPatternParameters(this);
}

void ViewProviderPathPattern::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit path pattern"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

const std::string & ViewProviderPathPattern::featureName() const
{
    static const std::string name = "PathPattern";
    return name;
}

PROPERTY_SOURCE(PartDesignGui::ViewProviderPointPattern, PartDesignGui::ViewProviderTransformed)

TaskDlgFeatureParameters *ViewProviderPointPattern::getEditDialog()
{
    return new TaskDlgPatternParameters(this);
}

void ViewProviderPointPattern::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit point pattern"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

const std::string & ViewProviderPointPattern::featureName() const
{
    static const std::string name = "PointPattern";
    return name;
}
