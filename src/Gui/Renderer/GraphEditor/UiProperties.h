//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//
// UIProperties and getUIProperties, copied from MaterialXRender/Util
// (the one thing the ported graph editor needs from a library the
// renderer does not build).

#ifndef MATERIALX_GRAPHEDITOR_UIPROPERTIES_H
#define MATERIALX_GRAPHEDITOR_UIPROPERTIES_H

#include <MaterialXCore/Document.h>

namespace Render::GraphEditor {

/// Set of possible UI properties for an element
struct UIProperties
{
    /// UI name
    std::string uiName;

    /// UI folder
    std::string uiFolder;

    /// Enumeration
    ::MaterialX::StringVec enumeration;

    /// Enumeration Values
    std::vector<::MaterialX::ValuePtr> enumerationValues;

    /// UI minimum value
    ::MaterialX::ValuePtr uiMin;

    /// UI maximum value
    ::MaterialX::ValuePtr uiMax;

    /// UI soft minimum value
    ::MaterialX::ValuePtr uiSoftMin;

    /// UI soft maximum value
    ::MaterialX::ValuePtr uiSoftMax;

    /// UI step value
    ::MaterialX::ValuePtr uiStep;

    /// UI advanced element
    bool uiAdvanced = false;
};

/// Get the UI properties for a given input element and target.
/// Returns the number of properties found.
unsigned int getUIProperties(::MaterialX::InputPtr input, const std::string& target, UIProperties& uiProperties);

} // namespace Render::GraphEditor

#endif
