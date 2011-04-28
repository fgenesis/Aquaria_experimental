/*
* Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef MANGOS_BYTECONVERTER_H
#define MANGOS_BYTECONVERTER_H

/** ByteConverter reverse your byte order.  This is use
for cross platform where they have different endians.
*/

#include <algorithm>

#include "SysDefs.h" // this is important to fix up any possible ***_ENDIAN misconfigurations

namespace ByteConverter
{
    template<size_t T>
    inline void convert(char *val)
    {
        std::swap(*val, *(val + T - 1));
        convert<T - 2>(val + 1);
    }

    template<> inline void convert<0>(char *) {}
    template<> inline void convert<1>(char *) {}            // ignore central byte

    template<typename T>
    inline void apply(T *val)
    {
        convert<sizeof(T)>((char *)(val));
    }
}

#if IS_BIG_ENDIAN
template<typename T> inline void ToLittleEndian(T& val) { ByteConverter::apply<T>(&val); }
template<typename T> inline void ToBigEndian(T&) { }
#else
template<typename T> inline void ToLittleEndian(T&) { }
template<typename T> inline void ToBigEndian(T& val) { ByteConverter::apply<T>(&val); }
#endif

template<typename T> void ToLittleEndian(T*);   // will generate link error
template<typename T> void ToBigEndian(T*);      // will generate link error

inline void ToLittleEndian(uint8&) { }
inline void ToLittleEndian(int8&)  { }
inline void ToBigEndian(uint8&) { }
inline void ToBigEndian( int8&) { }

#endif
