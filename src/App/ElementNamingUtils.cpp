#include "PreCompiled.h"

#include "ElementNamingUtils.h"
#include <boost/algorithm/string/predicate.hpp>

const std::string& Data::elementMapPrefix()
{
    static const std::string prefix = ";";
    return prefix;
}

const std::string& Data::missingPrefix()
{
    static const std::string prefix = "?";
    return prefix;
}

const std::string& Data::mappedChildPrefix()
{
    static const std::string prefix = elementMapPrefix() + ":R";
    return prefix;
}

const std::string& Data::tagPostfix()
{
    static const std::string tag = elementMapPrefix() + ":H";
    return tag;
}

const std::string& Data::decimalTagPostfix()
{
    static const std::string tag = elementMapPrefix() + ":T";
    return tag;
}

const std::string& Data::externalTagPostfix()
{
    static const std::string tag = elementMapPrefix() + ":X";
    return tag;
}

const std::string& Data::childTagPostfix()
{
    static const std::string tag = elementMapPrefix() + ":C";
    return tag;
}

const std::string& Data::indexPostfix()
{
    static const std::string tag = elementMapPrefix() + ":I";
    return tag;
}

const std::string& Data::upperPostfix()
{
    static const std::string tag = elementMapPrefix() + ":U";
    return tag;
}

const std::string& Data::lowerPostfix()
{
    static const std::string tag = elementMapPrefix() + ":L";
    return tag;
}

const std::string& Data::modPostfix()
{
    static const std::string tag = elementMapPrefix() + ":M";
    return tag;
}

const std::string& Data::genPostfix()
{
    static const std::string tag = elementMapPrefix() + ":G";
    return tag;
}

const std::string& Data::modgenPostfix()
{
    static const std::string tag = elementMapPrefix() + ":MG";
    return tag;
}

const std::string& Data::duplicatePostfix()
{
    static const std::string tag = elementMapPrefix() + "D";
    return tag;
}

const char *Data::isMappedElement(const char *name) {
    if(name && boost::starts_with(name, elementMapPrefix()))
        return name + elementMapPrefix().size();
    return nullptr;
}

std::string Data::newElementName(const char *name) {
    if(!name)
        return {};
    const char *dot = strrchr(name,'.');
    if(!dot || dot==name)
        return name;
    const char *c = dot-1;
    for(;c!=name;--c) {
        if(*c == '.') {
            ++c;
            break;
        }
    }
    if(isMappedElement(c))
        return std::string(name,dot-name);
    return name;
}

std::string Data::oldElementName(const char *name) {
    if(!name)
        return {};
    const char *dot = strrchr(name,'.');
    if(!dot || dot==name)
        return name;
    const char *c = dot-1;
    for(;c!=name;--c) {
        if(*c == '.') {
            ++c;
            break;
        }
    }
    if(isMappedElement(c))
        return std::string(name,c-name)+(dot+1);
    return name;
}

std::string Data::noElementName(const char *name) {
    if(!name)
        return {};
    auto element = findElementName(name);
    if(element)
        return std::string(name,element-name);
    return name;
}

const char *Data::findElementName(const char *subname) {
    if(!subname || !subname[0] || isMappedElement(subname))
        return subname;
    const char *dot = strrchr(subname,'.');
    if(!dot)
        return subname;
    const char *element = dot+1;
    if(dot==subname || isMappedElement(element))
        return element;
    for(--dot;dot!=subname;--dot) {
        if(*dot == '.') {
            ++dot;
            break;
        }
    }
    if(isMappedElement(dot))
        return dot;
    return element;
}

bool Data::hasMissingElement(const char *subname) {
    if(!subname)
        return false;
    auto dot = strrchr(subname,'.');
    if(dot)
        subname = dot+1;
    return boost::starts_with(subname, missingPrefix());
}

const char *Data::hasMappedElementName(const char *subname) {
    return isMappedElement(findElementName(subname));
}
