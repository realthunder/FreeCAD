/***************************************************************************
 *   Copyright (c) 2009 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#include "PreCompiled.h"
#ifndef _PreComp_
# include <cctype>
# include <cstdint>
# include <cstring>
# include <iomanip>
# include <sstream>
# include <locale>
# include <iostream>
# include <QElapsedTimer>
# include <QVector>
# include <QString>
#endif

#include "PyExport.h"
#include "Interpreter.h"
#include "Tools.h"

namespace Base
{
struct string_comp
{
    // s1 and s2 must be numbers represented as string
    bool operator()(const std::string& s1, const std::string& s2)
    {
        if (s1.size() < s2.size()) {
            return true;
        }
        if (s1.size() > s2.size()) {
            return false;
        }

        return s1 < s2;
    }
    static std::string increment(const std::string& s)
    {
        std::string n = s;
        int addcarry = 1;
        for (std::string::reverse_iterator it = n.rbegin(); it != n.rend(); ++it) {
            if (addcarry == 0) {
                break;
            }
            int d = *it - 48;
            d = d + addcarry;
            *it = ((d % 10) + 48);
            addcarry = d / 10;
        }
        if (addcarry > 0) {
            std::string b;
            b.resize(1);
            b[0] = addcarry + 48;
            n = b + n;
        }

        return n;
    }
};

class unique_name
{
public:
    unique_name(std::string name, int padding)
        : base_name {std::move(name)}
        , padding {padding}
    {
        removeDigitsFromEnd();
    }

    unique_name(std::string name, const std::vector<std::string>& names, int padding)
        : unique_name(std::move(name), padding)
    {
        for (const auto& used : names) {
            consider(used.c_str());
        }
    }

    /// Take one name that is already in use into account
    void consider(const char* name)
    {
        // same prefix?
        if (std::strncmp(name, base_name.c_str(), base_name.size()) != 0) {
            return;
        }
        const char* suffix = name + base_name.size();
        if (*suffix == '\0') {
            return;
        }
        std::size_t length = 0;
        for (const char* c = suffix; *c != '\0'; ++c, ++length) {
            if (*c < '0' || *c > '9') {
                return;
            }
        }
        // string_comp's order, spelled out on the characters themselves: a
        // longer run of digits is the larger number, and equal lengths compare
        // as text. Names are considered one per object in the document, so
        // this is worth not building a std::string for.
        if (num_suffix.size() < length
            || (num_suffix.size() == length && num_suffix.compare(suffix) < 0)) {
            num_suffix = suffix;
        }
    }

    std::string get() const
    {
        return appendSuffix();
    }

private:
    void removeDigitsFromEnd()
    {
        std::string::size_type pos = base_name.find_last_not_of("0123456789");
        if (pos != std::string::npos && (pos + 1) < base_name.size()) {
            num_suffix = base_name.substr(pos + 1);
            base_name.erase(pos + 1);
        }
    }

    std::string appendSuffix() const
    {
        std::stringstream str;
        str << base_name;
        if (padding > 0) {
            str.fill('0');
            str.width(padding);
        }
        str << Base::string_comp::increment(num_suffix);
        return str.str();
    }

private:
    std::string num_suffix;
    std::string base_name;
    int padding;
};

}  // namespace Base

std::string
Base::Tools::getUniqueName(const std::string& name, const std::vector<std::string>& names, int pad)
{
    if (names.empty()) {
        return name;
    }

    Base::unique_name unique(name, names, pad);
    return unique.get();
}

std::string
Base::Tools::getUniqueName(const std::string& name,
                           const std::function<const char*()>& next,
                           int pad)
{
    Base::unique_name unique(name, pad);
    bool any = false;
    while (const char* used = next()) {
        any = true;
        unique.consider(used);
    }
    if (!any) {
        return name;
    }
    return unique.get();
}

std::string Base::Tools::addNumber(const std::string& name, unsigned int num, int d)
{
    std::stringstream str;
    str << name;
    if (d > 0) {
        str.fill('0');
        str.width(d);
    }
    str << num;
    return str.str();
}

namespace
{

/// Length of the UTF-8 sequence starting at \a at, or 0 when what is there is
/// not the start of a valid one.
std::size_t utf8SequenceLength(const std::string& text, std::size_t at)
{
    const auto byte = static_cast<unsigned char>(text[at]);
    std::size_t length = 0;
    if ((byte & 0xE0) == 0xC0) {
        length = 2;
    }
    else if ((byte & 0xF0) == 0xE0) {
        length = 3;
    }
    else if ((byte & 0xF8) == 0xF0) {
        length = 4;
    }
    else {
        return 0;
    }
    if (at + length > text.size()) {
        return 0;
    }
    for (std::size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(text[at + i]) & 0xC0) != 0x80) {
            return 0;
        }
    }
    return length;
}

/// Whether Windows answers this name with a device rather than a file. What
/// follows a dot does not save it: `CON.Shape.brp` is the console.
bool isReservedDeviceName(const std::string& name)
{
    std::string head = name.substr(0, name.find('.'));
    for (char& c : head) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (head == "CON" || head == "PRN" || head == "AUX" || head == "NUL") {
        return true;
    }
    return head.size() == 4 && (head.compare(0, 3, "COM") == 0 || head.compare(0, 3, "LPT") == 0)
        && head[3] >= '0' && head[3] <= '9';
}

/// FNV-1a, spelt out rather than taken from std::hash: this ends up in a file
/// name someone's version control follows, so it has to be the same number in
/// every build.
std::string nameDigest(const std::string& text)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setw(8) << std::setfill('0') << (hash & 0xFFFFFFFFULL);
    return out.str();
}

}  // namespace

std::string Base::Tools::portableFileName(const std::string& name, std::size_t limit)
{
    std::string clean;
    clean.reserve(name.size());
    for (std::size_t i = 0; i < name.size();) {
        const auto byte = static_cast<unsigned char>(name[i]);
        if (byte < 0x80) {
            const bool reserved = byte < 0x20 || byte == 0x7F
                || std::strchr("<>:\"/\\|?*", static_cast<char>(byte)) != nullptr;
            clean += reserved ? '_' : static_cast<char>(byte);
            ++i;
            continue;
        }
        const std::size_t length = utf8SequenceLength(name, i);
        if (length == 0) {
            // Not UTF-8, so nothing downstream can be told what it is: Qt,
            // the zip entry table and the file system would each guess.
            clean += '_';
            ++i;
            continue;
        }
        clean.append(name, i, length);
        i += length;
    }

    if (clean.empty()) {
        return "_";
    }
    if (clean.front() == '.') {
        // A leading dot hides the file on every Unix, including from the
        // person looking for it in a directory project.
        clean.front() = '_';
    }
    for (std::size_t i = clean.size(); i > 0 && (clean[i - 1] == '.' || clean[i - 1] == ' '); --i) {
        clean[i - 1] = '_';
    }

    // The extension is kept through everything below, so a shortened name
    // still opens in whatever handles that type -- and it is taken in up to
    // two parts, because what says what a file is here is often two: a
    // `.Gui.xml` entry is dispatched on the whole of it.
    std::string ext;
    for (int part = 0; part < 2; ++part) {
        const std::size_t dot = clean.rfind('.');
        if (dot == std::string::npos || dot == 0 || clean.size() - dot - 1 > 8
            || clean.size() - dot < 2) {
            break;
        }
        bool plain = true;
        for (std::size_t i = dot + 1; plain && i < clean.size(); ++i) {
            const auto c = static_cast<unsigned char>(clean[i]);
            plain = c < 0x80 && std::isalnum(c) != 0;
        }
        if (!plain || clean.size() - dot + ext.size() > 16) {
            break;
        }
        ext = clean.substr(dot) + ext;
        clean.resize(dot);
    }

    if (isReservedDeviceName(clean)) {
        clean.insert(clean.begin(), '_');
    }

    if (clean.size() + ext.size() > limit) {
        const std::string digest = nameDigest(name);
        std::size_t room = limit > ext.size() + digest.size() + 1
            ? limit - ext.size() - digest.size() - 1
            : 1;
        if (room > clean.size()) {
            room = clean.size();
        }
        // Cut on a character boundary: half a UTF-8 sequence is not a name.
        while (room > 0 && (static_cast<unsigned char>(clean[room]) & 0xC0) == 0x80) {
            --room;
        }
        clean.resize(room);
        clean += '-';
        clean += digest;
    }

    if (clean.empty()) {
        clean = "_";
    }
    return clean + ext;
}

std::string Base::Tools::getIdentifier(const std::string& name)
{
    if (name.empty()) {
        return "_";
    }

    // Convert the given name into a PEP-3131 conforming Python identifier.
    // See: https://peps.python.org/pep-3131/#specification-of-language-changes

    auto CleanName = QString::fromUtf8(name.c_str()).toUcs4();

    // We'll replace all non Xid-Continue character as _. Special handling for
    // the first character. If it is non Xid-Start but a valid Xid-Continue,
    // insert an underscore as the new starting character.
    if (CleanName[0] != '_' && _PyUnicode_IsXidContinue(CleanName[0])
                            && !_PyUnicode_IsXidStart(CleanName[0])) {
        CleanName.push_front('_');
    }

    for (auto &c : CleanName) {
        if (!_PyUnicode_IsXidContinue(c))
            c = '_';
    }
    return QString::fromUcs4(&CleanName[0], CleanName.size()).toUtf8().constData();
}

std::wstring Base::Tools::widen(const std::string& str)
{
    std::wostringstream wstm;
    const std::ctype<wchar_t>& ctfacet = std::use_facet<std::ctype<wchar_t>>(wstm.getloc());
    for (char i : str) {
        wstm << ctfacet.widen(i);
    }
    return wstm.str();
}

std::string Base::Tools::narrow(const std::wstring& str)
{
    std::ostringstream stm;
    const std::ctype<char>& ctfacet = std::use_facet<std::ctype<char>>(stm.getloc());
    for (wchar_t i : str) {
        stm << ctfacet.narrow(i, 0);
    }
    return stm.str();
}

std::string Base::Tools::escapedUnicodeFromUtf8(const char* s)
{
    Base::PyGILStateLocker lock;
    std::string escapedstr;

    PyObject* unicode = PyUnicode_FromString(s);
    if (!unicode) {
        return escapedstr;
    }

    PyObject* escaped = PyUnicode_AsUnicodeEscapeString(unicode);
    if (escaped) {
        escapedstr = std::string(PyBytes_AsString(escaped));
        Py_DECREF(escaped);
    }

    Py_DECREF(unicode);
    return escapedstr;
}

std::string Base::Tools::escapedUnicodeToUtf8(const std::string& s)
{
    Base::PyGILStateLocker lock;
    std::string string;

    PyObject* unicode =
        PyUnicode_DecodeUnicodeEscape(s.c_str(), static_cast<Py_ssize_t>(s.size()), "strict");
    if (!unicode) {
        return string;
    }
    if (PyUnicode_Check(unicode)) {
        string = PyUnicode_AsUTF8(unicode);
    }
    Py_DECREF(unicode);
    return string;
}

QString Base::Tools::escapeEncodeString(const QString& s)
{
    QString result;
    const int len = s.length();
    result.reserve(int(len * 1.1));
    for (int i = 0; i < len; ++i) {
        if (s.at(i) == QLatin1Char('\\')) {
            result += QStringLiteral("\\\\");
        }
        else if (s.at(i) == QLatin1Char('\"')) {
            result += QStringLiteral("\\\"");
        }
        else if (s.at(i) == QLatin1Char('\'')) {
            result += QStringLiteral("\\\'");
        }
        else {
            result += s.at(i);
        }
    }
    result.squeeze();
    return result;
}

std::string Base::Tools::escapeEncodeString(const std::string& s)
{
    std::string result;
    size_t len = s.size();
    for (size_t i = 0; i < len; ++i) {
        if (s.at(i) == '\\') {
            result += "\\\\";
        }
        else if (s.at(i) == '\"') {
            result += "\\\"";
        }
        else if (s.at(i) == '\'') {
            result += "\\\'";
        }
        else {
            result += s.at(i);
        }
    }
    return result;
}

QString Base::Tools::escapeEncodeFilename(const QString& s)
{
    QString result;
    const int len = s.length();
    result.reserve(int(len * 1.1));
    for (int i = 0; i < len; ++i) {
        if (s.at(i) == QLatin1Char('\\')) {
            result += QStringLiteral("\\\\");
        }
        else if (s.at(i) == QLatin1Char('\"')) {
            result += QStringLiteral("\\\"");
        }
        else if (s.at(i) == QLatin1Char('\'')) {
            result += QStringLiteral("\\\'");
        }
        else {
            result += s.at(i);
        }
    }
    result.squeeze();
    return result;
}

std::string Base::Tools::escapeEncodeFilename(const std::string& s)
{
    std::string result;
    size_t len = s.size();
    for (size_t i = 0; i < len; ++i) {
        if (s.at(i) == '\\') {
            result += "\\\\";
        }
        else if (s.at(i) == '\"') {
            result += "\\\"";
        }
        else if (s.at(i) == '\'') {
            result += "\\\'";
        }
        else {
            result += s.at(i);
        }
    }
    return result;
}

std::string Base::Tools::pythonLiteral(const std::string& s)
{
    Base::PyGILStateLocker lock;
    std::string literal {"''"};

    PyObject* str = PyUnicode_FromStringAndSize(s.c_str(), static_cast<Py_ssize_t>(s.size()));
    if (!str) {
        PyErr_Clear();
        return literal;
    }

    // PyObject_ASCII is repr() with the non-ASCII escaped as well, so the
    // result is a complete literal that is safe to paste into a command
    // string whatever the interpreter's source encoding turns out to be.
    if (PyObject* repr = PyObject_ASCII(str)) {
        if (const char* text = PyUnicode_AsUTF8(repr)) {
            literal = text;
        }
        else {
            PyErr_Clear();
        }
        Py_DECREF(repr);
    }
    else {
        PyErr_Clear();
    }

    Py_DECREF(str);
    return literal;
}

std::string Base::Tools::pythonLiteral(const QString& s)
{
    return pythonLiteral(std::string(s.toUtf8().constData()));
}

std::string Base::Tools::quoted(const char* name)
{
    std::stringstream str;
    str << "\"" << name << "\"";
    return str.str();
}

std::string Base::Tools::quoted(const std::string& name)
{
    std::stringstream str;
    str << "\"" << name << "\"";
    return str.str();
}

std::string Base::Tools::joinList(const std::vector<std::string>& vec, const std::string& sep)
{
    std::stringstream str;
    for (const auto& it : vec) {
        str << it << sep;
    }
    return str.str();
}

std::vector<std::string> Base::Tools::splitSubName(const std::string& subname)
{
    // Turns 'Part.Part001.Body.Pad.Edge1'
    // Into ['Part', 'Part001', 'Body', 'Pad', 'Edge1']
    std::vector<std::string> subNames;
    std::string subName;
    std::istringstream subNameStream(subname);
    while (std::getline(subNameStream, subName, '.')) {
        subNames.push_back(subName);
    }

    // Check if the last character of the input string is the delimiter.
    // If so, add an empty string to the subNames vector.
    // Because the last subname is the element name and can be empty.
    if (!subname.empty() && subname.back() == '.') {
        subNames.push_back("");  // Append empty string for trailing dot.
    }

    return subNames;
}

// ----------------------------------------------------------------------------

using namespace Base;

struct StopWatch::Private
{
    QElapsedTimer t;
};

StopWatch::StopWatch()
    : d(new Private)
{}

StopWatch::~StopWatch()
{
    delete d;
}

void StopWatch::start()
{
    d->t.start();
}

int StopWatch::restart()
{
    return d->t.restart();
}

int StopWatch::elapsed()
{
    return d->t.elapsed();
}

std::string StopWatch::toString(int ms) const
{
    int total = ms;
    int msec = total % 1000;
    total = total / 1000;
    int secs = total % 60;
    total = total / 60;
    int mins = total % 60;
    int hour = total / 60;
    std::stringstream str;
    str << "Needed time: ";
    if (hour > 0) {
        str << hour << "h " << mins << "m " << secs << "s";
    }
    else if (mins > 0) {
        str << mins << "m " << secs << "s";
    }
    else if (secs > 0) {
        str << secs << "s";
    }
    else {
        str << msec << "ms";
    }
    return str.str();
}
