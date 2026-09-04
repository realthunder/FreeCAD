//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include "UiProperties.h"

#include <MaterialXGenShader/Util.h>

// std::runtime_error below: libstdc++ hands it over through another header,
// MSVC does not.
#include <stdexcept>

namespace Render::GraphEditor {

using namespace ::MaterialX;

unsigned int getUIProperties(InputPtr input, const string& target, UIProperties& uiProperties)
{
    if (!input)
    {
        return 0;
    }
    InputPtr nodeDefInput = getNodeDefInput(input, target);
    if (nodeDefInput)
    {
        input = nodeDefInput;
    }

    unsigned int propertyCount = 0;
    uiProperties.uiName = input->getAttribute(ValueElement::UI_NAME_ATTRIBUTE);
    if (!uiProperties.uiName.empty())
    {
        propertyCount++;
    }

    uiProperties.uiFolder = input->getAttribute(ValueElement::UI_FOLDER_ATTRIBUTE);
    if (!uiProperties.uiFolder.empty())
    {
        propertyCount++;
    }

    if (input->getIsUniform())
    {
        uiProperties.enumeration = input->getTypedAttribute<StringVec>(ValueElement::ENUM_ATTRIBUTE);
        if (!uiProperties.enumeration.empty())
        {
            propertyCount++;
        }

        const string& enumValuesAttr = input->getAttribute(ValueElement::ENUM_VALUES_ATTRIBUTE);
        if (!enumValuesAttr.empty())
        {
            const string COMMA_SEPARATOR = ",";
            string valueString;
            size_t index = 0;
            vector<string> enumValuesParts = splitString(enumValuesAttr, COMMA_SEPARATOR);

            // Infer the size of the type by making an assumption the correct number of elements
            // are provided in the value.
            size_t typeDescSize = enumValuesParts.size() / uiProperties.enumeration.size();

            for (const string& val : enumValuesParts)
            {
                if (index < typeDescSize - 1)
                {
                    valueString += val + COMMA_SEPARATOR;
                    index++;
                }
                else
                {
                    valueString += val;
                    uiProperties.enumerationValues.push_back(Value::createValueFromStrings(valueString, input->getType()));
                    valueString.clear();
                    index = 0;
                }
            }
            if (uiProperties.enumeration.size() != uiProperties.enumerationValues.size())
            {
                throw std::runtime_error("Every enum must have a value!");
            }
            propertyCount++;
        }
    }

    const string& uiMinString = input->getAttribute(ValueElement::UI_MIN_ATTRIBUTE);
    if (!uiMinString.empty())
    {
        ValuePtr value = Value::createValueFromStrings(uiMinString, input->getType());
        if (value)
        {
            uiProperties.uiMin = value;
            propertyCount++;
        }
    }

    const string& uiMaxString = input->getAttribute(ValueElement::UI_MAX_ATTRIBUTE);
    if (!uiMaxString.empty())
    {
        ValuePtr value = Value::createValueFromStrings(uiMaxString, input->getType());
        if (value)
        {
            uiProperties.uiMax = value;
            propertyCount++;
        }
    }

    const string& uiSoftMinString = input->getAttribute(ValueElement::UI_SOFT_MIN_ATTRIBUTE);
    if (!uiSoftMinString.empty())
    {
        ValuePtr value = Value::createValueFromStrings(uiSoftMinString, input->getType());
        if (value)
        {
            uiProperties.uiSoftMin = value;
            propertyCount++;
        }
    }

    const string& uiSoftMaxString = input->getAttribute(ValueElement::UI_SOFT_MAX_ATTRIBUTE);
    if (!uiSoftMaxString.empty())
    {
        ValuePtr value = Value::createValueFromStrings(uiSoftMaxString, input->getType());
        if (value)
        {
            uiProperties.uiSoftMax = value;
            propertyCount++;
        }
    }

    const string& uiStepString = input->getAttribute(ValueElement::UI_STEP_ATTRIBUTE);
    if (!uiStepString.empty())
    {
        ValuePtr value = Value::createValueFromStrings(uiStepString, input->getType());
        if (value)
        {
            uiProperties.uiStep = value;
            propertyCount++;
        }
    }

    const string& uiAdvancedString = input->getAttribute(ValueElement::UI_ADVANCED_ATTRIBUTE);
    uiProperties.uiAdvanced = (uiAdvancedString == "true");
    if (!uiAdvancedString.empty())
    {
        propertyCount++;
    }

    return propertyCount;
}

} // namespace Render::GraphEditor
