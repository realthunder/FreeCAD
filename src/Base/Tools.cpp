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

namespace Base {
struct string_comp
{
    // s1 and s2 must be numbers represented as string
    bool operator()(const std::string& s1, const std::string& s2)
    {
        if (s1.size() < s2.size())
            return true;
        else if (s1.size() > s2.size())
            return false;
        else
            return s1 < s2;
    }
    static std::string increment(const std::string& s)
    {
        std::string n = s;
        int addcarry=1;
        for (std::string::reverse_iterator it = n.rbegin(); it != n.rend(); ++it) {
            if (addcarry == 0)
                break;
            int d = *it - 48;
            d = d + addcarry;
            *it = ((d%10) + 48);
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
}

std::string Base::Tools::getUniqueName(const std::string& name, const std::vector<std::string>& names, int d)
{
    // find highest suffix
    bool matched = false;
    std::string num_suffix;
    for (std::vector<std::string>::const_iterator it = names.begin(); it != names.end(); ++it) {
        if (it->substr(0, name.length()) == name) { // same prefix
            matched = true;
            std::string suffix(it->substr(name.length()));
            if (suffix.size() > 0) {
                std::string::size_type pos = suffix.find_first_not_of("0123456789");
                if (pos==std::string::npos)
                    num_suffix = std::max<std::string>(num_suffix, suffix, Base::string_comp());
            }
        }
    }

    std::stringstream str;
    str << name;
    if (d > 0) {
        str.fill('0');
        str.width(d);
    }
    if (matched)
        str << Base::string_comp::increment(num_suffix);
    return str.str();
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

std::string Base::Tools::getIdentifier(const std::string& name)
{
    if (name.empty())
          return name;

    // Convert the given name into a PEP-3131 conforming Python identifier.
    // See: https://peps.python.org/pep-3131/#specification-of-language-changes

    auto CleanName = QString::fromUtf8(name.c_str()).toUcs4();

    // We'll replace all non Xid-Continue character as _. Special handling for
    // the first character. If it is non Xid-Start but a valid Xid-Continue,
    // insert an underscore as the new starting character.
    if (CleanName[0] != '_' && _PyUnicode_IsXidContinue(CleanName[0])
                            && !_PyUnicode_IsXidStart(CleanName[0]))
        CleanName.push_front('_');

    for (auto &c : CleanName) {
        if (!_PyUnicode_IsXidContinue(c))
            c = '_';
    }
    return QString::fromUcs4(&CleanName[0], CleanName.size()).toUtf8().constData();
}

std::wstring Base::Tools::widen(const std::string& str)
{
    std::wostringstream wstm;
    const std::ctype<wchar_t>& ctfacet = std::use_facet< std::ctype<wchar_t> >(wstm.getloc());
    for (size_t i=0; i<str.size(); ++i)
        wstm << ctfacet.widen(str[i]);
    return wstm.str();
}

std::string Base::Tools::narrow(const std::wstring& str)
{
    std::ostringstream stm;
    const std::ctype<char>& ctfacet = std::use_facet< std::ctype<char> >(stm.getloc());
    for (size_t i=0; i<str.size(); ++i)
        stm << ctfacet.narrow(str[i], 0);
    return stm.str();
}

std::string Base::Tools::escapedUnicodeFromUtf8(const char *s)
{
    Base::PyGILStateLocker lock;
    std::string escapedstr;

    PyObject* unicode = PyUnicode_FromString(s);
    if (!unicode)
        return escapedstr;

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

    PyObject* unicode = PyUnicode_DecodeUnicodeEscape(s.c_str(), s.size(), "strict");
    if (!unicode)
        return string;
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
        if (s.at(i) == QLatin1Char('\\'))
            result += QStringLiteral("\\\\");
        else if (s.at(i) == QLatin1Char('\"'))
            result += QStringLiteral("\\\"");
        else if (s.at(i) == QLatin1Char('\''))
            result += QStringLiteral("\\\'");
        else
            result += s.at(i);
    }
    result.squeeze();
    return result;
}

std::string Base::Tools::escapeEncodeString(const std::string& s)
{
    std::string result;
    size_t len = s.size();
    for (size_t i = 0; i < len; ++i) {
        if (s.at(i) == '\\')
            result += "\\\\";
        else if (s.at(i) == '\"')
            result += "\\\"";
        else if (s.at(i) == '\'')
            result += "\\\'";
        else
            result += s.at(i);
    }
    return result;
}

QString Base::Tools::escapeEncodeFilename(const QString& s)
{
    QString result;
    const int len = s.length();
    result.reserve(int(len * 1.1));
    for (int i = 0; i < len; ++i) {
        if (s.at(i) == QLatin1Char('\\'))
            result += QStringLiteral("\\\\");
        else if (s.at(i) == QLatin1Char('\"'))
            result += QStringLiteral("\\\"");
        else if (s.at(i) == QLatin1Char('\''))
            result += QStringLiteral("\\\'");
        else
            result += s.at(i);
    }
    result.squeeze();
    return result;
}

std::string Base::Tools::escapeEncodeFilename(const std::string& s)
{
    std::string result;
    size_t len = s.size();
    for (size_t i = 0; i < len; ++i) {
        if (s.at(i) == '\\')
            result += "\\\\";
        else if (s.at(i) == '\"')
            result += "\\\"";
        else if (s.at(i) == '\'')
            result += "\\\'";
        else
            result += s.at(i);
    }
    return result;
}

// ----------------------------------------------------------------------------

using namespace Base;

struct StopWatch::Private
{
    QElapsedTimer t;
};

StopWatch::StopWatch() : d(new Private)
{
}

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
    if (hour > 0)
        str << hour << "h " << mins << "m " << secs << "s";
    else if (mins > 0)
        str << mins << "m " << secs << "s";
    else if (secs > 0)
        str << secs << "s";
    else
        str << msec << "ms";
    return str.str();
}
