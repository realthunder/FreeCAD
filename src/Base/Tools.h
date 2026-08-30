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


#ifndef BASE_TOOLS_H
#define BASE_TOOLS_H

#ifndef FC_GLOBAL_H
#include <FCGlobal.h>
#endif
#include <functional>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <fastsignals/signal.h>
#ifndef FC_NO_QT
#include <QString>
#endif

#include "Exception.h"
#include "Console.h"

// ----------------------------------------------------------------------------

namespace Base
{

template<class T>
struct iotaGen
{
public:
    T operator()()
    {
        return n++;
    }
    explicit iotaGen(T v)
        : n(v)
    {}

private:
    T n;
};

// ----------------------------------------------------------------------------

template<class T>
class manipulator
{
    T i_;
    std::ostream& (*f_)(std::ostream&, T);

public:
    manipulator(std::ostream& (*f)(std::ostream&, T), T i)
        : i_(i)
        , f_(f)
    {}
    friend std::ostream& operator<<(std::ostream& os, manipulator m)
    {
        return m.f_(os, m.i_);
    }
};

inline std::ostream& tabsN(std::ostream& os, int n)
{
    for (int i = 0; i < n; i++) {
        os << "\t";
    }
    return os;
}

inline std::ostream& blanksN(std::ostream& os, int n)
{
    for (int i = 0; i < n; i++) {
        os << " ";
    }
    return os;
}

inline manipulator<int> tabs(int n)
{
    return {&tabsN, n};
}

inline manipulator<int> blanks(int n)
{
    return {&blanksN, n};
}

// ----------------------------------------------------------------------------

template<class T>
inline T clamp(T num, T lower, T upper)
{
    return std::max<T>(std::min<T>(upper, num), lower);
}

template<class T>
inline T sgn(T t)
{
    if (t == 0) {
        return T(0);
    }

    return (t > 0) ? T(1) : T(-1);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template<class T>
inline T toRadians(T d)
{
    return static_cast<T>((d * M_PI) / 180.0);
}

template<class T>
inline T toDegrees(T r)
{
    return static_cast<T>((r / M_PI) * 180.0);
}

/** Transparency and the like are a percent in the property, a fraction in
 * the scene graph. Upstream spells the two conversions this way, and every
 * ported call site that touches a percentage expects them.
 */
inline float fromPercent(const long value)
{
    return std::roundf(static_cast<float>(value)) / 100.0F;
}

inline long toPercent(float value)
{
    return std::lround(100.0 * value);
}

template<class T>
inline T fmod(T numerator, T denominator)
{
    T modulo = std::fmod(numerator, denominator);
    return (modulo >= T(0)) ? modulo : modulo + denominator;
}

// copied from boost::hash_combine. 
template <class S, class T>
inline void hash_combine(S& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

// ----------------------------------------------------------------------------

class BaseExport StopWatch
{
public:
    StopWatch();
    ~StopWatch();

    void start();
    int restart();
    int elapsed();
    std::string toString(int ms) const;

    StopWatch(const StopWatch&) = delete;
    StopWatch(StopWatch&&) = delete;
    StopWatch& operator=(const StopWatch&) = delete;
    StopWatch& operator=(StopWatch&&) = delete;

private:
    struct Private;
    Private* d;
};

// ----------------------------------------------------------------------------

// NOLINTBEGIN
template<typename Flag = bool>
struct FlagToggler
{

    Flag& flag;
    bool toggled;

    FlagToggler(Flag& _flag)
        : flag(_flag)
        , toggled(true)
    {
        flag = !flag;
    }

    FlagToggler(Flag& _flag, Flag check)
        : flag(_flag)
        , toggled(check == _flag)
    {
        if (toggled) {
            flag = !flag;
        }
    }

    ~FlagToggler()
    {
        if (toggled) {
            flag = !flag;
        }
    }
};

// ----------------------------------------------------------------------------

template<typename Status, class Object>
class ObjectStatusLocker
{
public:
    ObjectStatusLocker(Status s, Object* o, bool value = true)
        : status(s)
        , obj(o)
    {
        old_value = obj->testStatus(status);
        obj->setStatus(status, value);
    }
    ~ObjectStatusLocker()
    { 
        if (obj) {
            obj->setStatus(status, old_value);
        }
    }

    void detach(bool reset=true) {
        if (obj) {
            if (reset) {
                obj->setStatus(status, old_value); 
            }
            obj = nullptr;
        }
    }

private:
    Status status;
    Object* obj;
    bool old_value;
};

// ----------------------------------------------------------------------------

class StateLocker
{
public:
    StateLocker(bool& flag, bool value = true)
        : lock(flag)
    {
        old_value = lock;
        lock = value;
    }  // NOLINT
    ~StateLocker()
    {
        lock = old_value;
    }

private:
    bool& lock;
    bool old_value;
};

// ----------------------------------------------------------------------------

template<typename T>
class BitsetLocker
{
public:
    BitsetLocker(T& flags, std::size_t flag, bool value = true)
        : flags(flags)
        , flag(flag)
    {
        oldValue = flags.test(flag);
        flags.set(flag, value);
    }
    ~BitsetLocker()
    {
        flags.set(flag, oldValue);
    }

private:
    T& flags;
    std::size_t flag;
    bool oldValue;
};

// ----------------------------------------------------------------------------

/** Blocks a connection for as long as it is alive
 *
 * The connection has to have been made with fastsignals::advanced_tag: a
 * blockable slot is wrapped in a check at connect time, so unlike
 * Boost.Signals2 the ability to block is a property of the connection and
 * cannot be added afterwards. Base::AdvancedConnection is the type that
 * carries it.
 */
class ConnectionBlocker
{
    using Connection = fastsignals::advanced_connection;
    using ConnectionBlock = fastsignals::shared_connection_block;
    ConnectionBlock blocker;

public:
    ConnectionBlocker(Connection& c)
        : blocker(c)
    {}
    ~ConnectionBlocker() = default;
};
// NOLINTEND

// ----------------------------------------------------------------------------

/** Temporary shorten a sub-object path for more efficient traversal */
class StringGuard {
public:
    StringGuard(char *c)
        :c(c)
    {
        v1 = c[0];
        v2 = c[1];
        c[0] = '.';
        c[1] = 0;
    }
    ~StringGuard()
    {
        c[0] = v1;
        c[1] = v2;
    }

    char *c;
    char v1;
    char v2;
};


// ----------------------------------------------------------------------------

struct BaseExport Tools
{
    static std::string
    getUniqueName(const std::string&, const std::vector<std::string>&, int d = 0);
    /** getUniqueName over names handed out one at a time
     *
     * \a next returns the next name in use, or nullptr once there are none
     * left. A caller that already holds those names elsewhere -- a document
     * holds a label per object -- would otherwise have to copy every one of
     * them into a vector first, and with thousands of them that copy is the
     * whole cost of the call.
     */
    static std::string
    getUniqueName(const std::string&, const std::function<const char*()>& next, int d = 0);
    static std::string addNumber(const std::string&, unsigned int, int d = 0);
    static std::string getIdentifier(const std::string&);

    /** A file name every platform will take, out of a name someone chose.
     *
     * Objects and properties are named by whoever made them, and those names
     * reach the file system: a project saved as a directory writes one file
     * per object and one per stored shape, named after them
     * (docs/SharedShapeStorage.md sec 12.1). getIdentifier() has already made
     * an object's name a Python identifier, which rules out a separator and a
     * space -- and leaves everything a file system cares about and Python
     * does not.
     *
     *  - Non-ASCII is kept. It is legal on every platform this runs on, and
     *    a derived name exists to be read.
     *  - Past 255 bytes a name stops being a name -- ext4 and APFS refuse it
     *    and the write simply fails, which is how a long object name lost its
     *    shape and its XML without anything the user could see. A name over
     *    \a limit is cut on a character boundary and given a digest of what
     *    it was, so two long names that agree up to the cut stay two files.
     *  - Windows reserves CON, PRN, AUX, NUL, COM0-9 and LPT0-9 *including*
     *    with anything after a dot: `CON.Shape.brp` is the console.
     *  - Windows drops a trailing dot or space, so a name ending in one is
     *    not the name it opens.
     *  - Anything a file system reserves outright, and any byte that is not
     *    valid UTF-8, becomes an underscore.
     *
     * The extension, if the name ends in a short alphanumeric one, is kept
     * through all of it. Idempotent: a name this already answers with comes
     * back unchanged, which is what lets it be applied at every layer that
     * makes a file name without the layers fighting.
     */
    static std::string portableFileName(const std::string& name, std::size_t limit = 120);
    static std::wstring widen(const std::string& str);
    static std::string narrow(const std::wstring& str);
    static std::string escapedUnicodeFromUtf8(const char* s);
    static std::string escapedUnicodeToUtf8(const std::string& s);

#ifndef FC_NO_QT
    static QString escapeEncodeString(const QString& s);
    static QString escapeEncodeFilename(const QString& s);
#endif
    static std::string escapeEncodeString(const std::string& s);
    static std::string escapeEncodeFilename(const std::string& s);

    /**
     * @brief pythonLiteral Render a string as Python source: a complete
     * literal, quotes included, that evaluates back to exactly this string.
     *
     * Use this to put a value -- a file path above all -- into a command
     * string handed to the interpreter, and do not add quotes of your own.
     * It is written by Python itself, so unlike escapeEncodeString() and
     * escapeEncodeFilename(), which escape only backslash and the two
     * quotes, it also survives a newline or a tab in a file name (legal on
     * Linux and macOS) and leaves nothing but ASCII behind.
     *
     * @param s String to render.
     * @return A quoted Python literal; "''" if the string cannot be encoded.
     */
    static std::string pythonLiteral(const std::string& s);
#ifndef FC_NO_QT
    static std::string pythonLiteral(const QString& s);
#endif

#ifndef FC_NO_QT
    /**
     * @brief toStdString Convert a QString into a UTF-8 encoded std::string.
     * @param s String to convert.
     * @return A std::string encoded as UTF-8.
     */
    static inline std::string toStdString(const QString& s)
    {
        QByteArray tmp = s.toUtf8();
        return {tmp.constData(), static_cast<size_t>(tmp.size())};
    }

    /**
     * @brief fromStdString Convert a std::string encoded as UTF-8 into a QString.
     * @param s std::string, expected to be UTF-8 encoded.
     * @return String represented as a QString.
     */
    static inline QString fromStdString(const std::string& s)
    {
        return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
    }
#endif

    /**
     * @brief quoted Creates a quoted string.
     * @param String to be quoted.
     * @return A quoted std::string.
     */
    static std::string quoted(const char*);
    /**
     * @brief quoted Creates a quoted string.
     * @param String to be quoted.
     * @return A quoted std::string.
     */
    static std::string quoted(const std::string&);

    /**
     * @brief isNullOrEmpty
     * @param str A C string, possibly null.
     * @return true if \a str is null or has no characters.
     */
    static constexpr bool isNullOrEmpty(const char* str)
    {
        return !str || str[0] == '\0';
    }

    /**
     * @brief joinList
     * Join the vector of strings \a vec using the separator \a sep
     * @param vec
     * @param sep
     * @return
     */
    static std::string joinList(const std::vector<std::string>& vec, const std::string& sep = ", ");

    /**
     * @brief splitSubName
     * Split a dot separated sub-object path into its components. A trailing dot
     * yields a final empty component, since the last one is the element name and
     * may be empty.
     * 'Part.Body.Pad.Edge1' -> ['Part', 'Body', 'Pad', 'Edge1']
     */
    static std::vector<std::string> splitSubName(const std::string& subname);
};


}  // namespace Base

#endif  // BASE_TOOLS_H
