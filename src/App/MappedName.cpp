/****************************************************************************
 *   Copyright (c) 2020 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <unordered_set>
#endif

#include "MappedName.h"

#include "Base/Console.h"

//#include <boost/functional/hash.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

FC_LOG_LEVEL_INIT("MappedName", true, 2);// NOLINT

using namespace App;
using namespace Data;

void MappedName::compact() const
{
    auto self = const_cast<MappedName*>(this); //FIXME this is a workaround for a single call in ElementMap::addName()

    if (this->raw) {
        self->data = QByteArray(self->data.constData(), self->data.size());
        self->raw = false;
    }

#if 0
    static std::unordered_set<QByteArray, ByteArrayHasher> PostfixSet;
    if (this->postfix.size()) {
        auto res = PostfixSet.insert(this->postfix);
        if (!res.second)
            self->postfix = *res.first;
    }
#endif
}


int MappedName::findTagInElementName(long* tagOut, int* lenOut, std::string* postfixOut,
                                     char* typeOut, bool negative, bool recursive) const
{
    bool hex = true;
    int pos = this->rfind(tagPostfix());

    // Example name, tagPostfix() == ;:H
    // #94;:G0;XTR;:H19:8,F;:H1a,F;BND:-1:0;:H1b:10,F
    //                                     ^
    //                                     |
    //                                    pos

    if(pos < 0) {
        pos = this->rfind(decimalTagPostfix());
        if (pos < 0) {
            return -1;
        }
        hex = false;
    }
    int offset = pos + (int)tagPostfix().size();
    long _tag = 0;
    int _len = 0;
    char sep = 0;
    char sep2 = 0;
    char tp = 0;
    char eof = 0;

    int size {0};
    const char * nameAsChars = this->toConstString(offset, size);

    // check if the number followed by the tagPosfix is negative
    bool isNegative = (nameAsChars[0] == '-');
    if (isNegative) {
        ++nameAsChars;
        --size;
    }
    boost::iostreams::stream<boost::iostreams::array_source> iss(nameAsChars, size);
    if (!hex) {
        // no hex is an older version of the encoding scheme
        iss >> _tag >> sep;
    } else {
        // The purpose of tagOut postfixOut is to encode one model operation. The
        // 'tagOut' field is used to record the own object ID of that model shape,
        // and the 'lenOut' field indicates the length of the operation codes
        // before the tagOut postfixOut. These fields are in hex. The trailing 'F' is
        // the shape typeOut of this element, 'F' for face, 'E' edge, and 'V' vertex.
        //
        // #94;:G0;XTR;:H19:8,F;:H1a,F;BND:-1:0;:H1b:10,F
        //                     |              |   ^^ ^^
        //                     |              |   |   |
        //                     ---lenOut = 0x10---  tagOut lenOut

        iss >> std::hex;
        // _tag field can be skipped, if it is 0
        if (nameAsChars[0] == ',' || nameAsChars[0] == ':') {
            iss >> sep;
        }
        else {
            iss >> _tag >> sep;
        }
    }

    if (isNegative) {
        _tag = -_tag;
    }

    if (sep == ':') {
        // ':' is followed by _len field.
        //
        // For decTagPostfix() (i.e. older encoding scheme), this is the length
        // of the string before the entire postfixOut (A postfixOut may contain
        // multiple segments usually separated by elementMapPrefix().
        //
        // For newer tagPostfix(), this counts the number of characters that
        // proceeds this tagOut postfixOut segment that forms the op code (see
        // example above).
        //
        // The reason of this change is so that the postfixOut can stay the same
        // regardless of the prefix, which can increase memory efficiency.
        //
        iss >> _len >> sep2 >> tp >> eof;

        // The next separator to look for is either ':' for older tagOut postfixOut, or ','
        if (!hex && sep2 == ':') {
            sep2 = ',';
        }
    }
    else if (hex && sep == ',') {
        // ',' is followed by a single character that indicates the element typeOut.
        iss >> tp >> eof;
        sep = ':';
        sep2 = ',';
    }

    if (_len < 0 || sep != ':' || sep2 != ',' || tp == 0 || eof != 0) {
        return -1;
    }

    if (hex) {
        if (pos-_len < 0) {
            return -1;
        }
        if ((_len != 0) && recursive && (tagOut || lenOut)) {
            // in case of recursive tagOut postfixOut (used by hierarchy element
            // map), look for any embedded tagOut postfixOut
            int next = MappedName::fromRawData(*this, pos-_len, _len).rfind(tagPostfix());
            if (next >= 0) {
                next += pos - _len;
                // #94;:G0;XTR;:H19:8,F;:H1a,F;BND:-1:0;:H1b:10,F
                //                     ^               ^
                //                     |               |
                //                    next            pos
                //
                // There maybe other operation codes after this embedded tagOut
                // postfixOut, search for the separator.
                //
                int end {0};
                if (pos == next) {
                    end = -1;
                }
                else {
                    end = MappedName::fromRawData(*this, next + 1, pos - next - 1)
                              .find(elementMapPrefix());
                }
                if (end >= 0) {
                    end += next+1;
                    // #94;:G0;XTR;:H19:8,F;:H1a,F;BND:-1:0;:H1b:10,F
                    //                            ^
                    //                            |
                    //                           end
                    _len = pos - end;
                    // #94;:G0;XTR;:H19:8,F;:H1a,F;BND:-1:0;:H1b:10,F
                    //                            |       |
                    //                            -- lenOut --
                } else {
                    _len = 0;
                }
            }
        }

        // Now convert the 'lenOut' field back to the length of the remaining name
        //
        // #94;:G0;XTR;:H19:8,F;:H1a,F;BND:-1:0;:H1b:10,F
        // |                         |
        // ----------- lenOut -----------
        _len = pos - _len;
    }
    if(typeOut) {
        *typeOut = tp;
    }
    if(tagOut) {
        if (_tag == 0 && recursive) {
            return MappedName(*this, 0, _len)
                .findTagInElementName(tagOut, lenOut, postfixOut, typeOut, negative);
        }
        if(_tag>0 || negative) {
            *tagOut = _tag;
        }
        else {
            *tagOut = -_tag;
        }
    }
    if(lenOut) {
        *lenOut = _len;
    }
    if(postfixOut) {
        *postfixOut = this->toString(pos);
    }
    return pos;
}

MappedName MappedName::hashElementName(StringHasherRef hasher, ElementIDRefs &sids) const
{
    if(!hasher || empty()) {
        return *this;
    }
    if (find(elementMapPrefix()) < 0) {
        return *this;
    }
    App::StringIDRef sid = hasher->getID(*this, sids);
    const auto &related = sid.relatedIDs();
    if (related == sids) {
        sids.clear();
        sids.push_back(sid);
    } else {
        ElementIDRefs tmp;
        tmp.push_back(sid);
        for (auto &s : sids) {
            if (related.indexOf(s) < 0)
                tmp.push_back(s);
        }
        sids = tmp;
    }
    return MappedName{sid.toString()};
}

MappedName MappedName::dehashElementName(const StringHasherRef & hasher) const
{
    if(empty()) {
        return *this;
    }
    if(!hasher) {
        return *this;
    }
    auto id = App::StringID::fromString(toRawBytes());
    if(!id) {
        return *this;
    }
    auto sid = hasher->getID(id);
    if(!sid) {
        if(FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_TRACE))
            FC_WARN("failed to find hash id " << id);
        else
            FC_LOG("failed to find hash id " << id);
        return *this;
    }
    if(sid.isHashed()) {
        FC_LOG("cannot dehash id " << id);
        return *this;
    }
    MappedName ret(sid);
    FC_TRACE("dehash " << *this << " -> " << ret);
    return ret;
}


bool ElementNameComp::operator()(const MappedName &a, const MappedName &b) const
{
    size_t size = std::min(a.size(),b.size());
    if(!size)
        return a.size()<b.size();
    size_t i=0;
    if(b[0] == '#') {
        if(a[0]!='#')
            return true;
        // If both string starts with '#', compare the following hex digits by
        // its integer value.
        int res = 0;
        for(i=1;i<size;++i) {
            unsigned char ac = (unsigned char)a[i];
            unsigned char bc = (unsigned char)b[i];
            if(std::isxdigit(bc)) {
                if(!std::isxdigit(ac))
                    return true;
                if(res==0) {
                    if(ac<bc)
                        res = -1;
                    else if(ac>bc)
                        res = 1;
                }
            }else if(std::isxdigit(ac))
                return false;
            else
                break;
        }
        if(res < 0)
            return true;
        else if(res > 0)
            return false;

        for (; i<size; ++i) {
            char ac = a[i];
            char bc = b[i];
            if (ac < bc)
                return true;
            if (ac > bc)
                return false;
        }
        return a.size()<b.size();
    }
    else if (a[0] == '#')
        return false;

    // If the string does not start with '#', compare the non-digits prefix
    // using lexical order.
    for(i=0;i<size;++i) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if(!std::isdigit(bc)) {
            if(std::isdigit(ac))
                return true;
            if(ac<bc)
                return true;
            if(ac>bc)
                return false;
        } else if(!std::isdigit(ac)) {
            return false;
        } else
            break;
    }

    // Then compare the following digits part by integer value
    int res = 0;
    for(;i<size;++i) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if(std::isdigit(bc)) {
            if(!std::isdigit(ac))
                return true;
            if(res==0) {
                if(ac<bc)
                    res = -1;
                else if(ac>bc)
                    res = 1;
            }
        }else if(std::isdigit(ac))
            return false;
        else
            break;
    }
    if(res < 0)
        return true;
    else if(res > 0)
        return false;

    // Finally, compare the remaining tail using lexical order
    for (; i<size; ++i) {
        char ac = a[i];
        char bc = b[i];
        if (ac < bc)
            return true;
        if (ac > bc)
            return false;
    }
    return a.size()<b.size();
}
