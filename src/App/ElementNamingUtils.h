#ifndef ELEMENT_NAMING_UTILS_H
#define ELEMENT_NAMING_UTILS_H

#include <string>
#include "FCGlobal.h"


namespace Data
{

/// Special prefix to mark the beginning of a mapped sub-element name
const std::string& elementMapPrefix();

/// Special prefix to mark a missing element
const std::string& missingPrefix();

const std::string& mappedChildPrefix();

/// Special postfix to mark the following tag
const std::string& tagPostfix();

const std::string& decimalTagPostfix();
const std::string& externalTagPostfix();
const std::string& childTagPostfix();

/// Special postfix to mark the index of an array element
const std::string& indexPostfix();
const std::string& upperPostfix();
const std::string& lowerPostfix();
const std::string& modPostfix();
const std::string& genPostfix();
const std::string& modgenPostfix();
const std::string& duplicatePostfix();


/// Check if a subname contains missing element
AppExport bool hasMissingElement(const char *subname);

/** Check if the name starts with elementMapPrefix()
 *
 * @param name: input name
 * @return Returns the name stripped with elementMapPrefix(), or 0 if not
 * start with the prefix
 */
AppExport const char *isMappedElement(const char *name);

/// Strip out the trailing element name if there is mapped element name precedes it.
AppExport std::string newElementName(const char *name);

/// Strip out the mapped element name if there is one.
AppExport std::string oldElementName(const char *name);

/// Strip out the old and new element name if there is one.
AppExport std::string noElementName(const char *name);

/// Find the start of an element name in a subname
AppExport const char *findElementName(const char *subname);

/// Check if the given subname only contains an element name
inline bool isElementName(const char *subname)
{
    return subname && *subname && findElementName(subname)==subname;
}

/// Check if the given subname contains element name
inline bool hasElementName(const char *subname)
{
    subname = findElementName(subname);
    return subname && *subname;
}

AppExport const char *hasMappedElementName(const char *subname);


}// namespace Data

#endif // ELEMENT_NAMING_UTILS_H
