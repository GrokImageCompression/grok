/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 */

#pragma once
#include <stdexcept>

namespace grk
{
class DecodeUnknownMarkerAtEndOfTileException : public std::exception
{
};
class PluginDecodeUnsupportedException : public std::exception
{
};
class CorruptJP2BoxException : public std::exception
{
};
class TruncatedPacketHeaderException : public std::exception
{
};
class SparseBufferOverrunException : public std::exception
{
};
class CorruptPacketException : public std::exception
{
};
class CorruptPacketHeaderException : public CorruptPacketException
{
};
class CorruptPacketDataException : public CorruptPacketException
{
};
class InvalidMarkerException : public std::exception
{
 public:
   explicit InvalidMarkerException(uint16_t marker) : marker_(marker) {}

   uint16_t marker_;
};
class BadAsocException : public std::exception
{
};
class CorruptMarkerException : public std::exception
{
};
class CorruptTLMException : public CorruptMarkerException
{
};
class CorruptSOTMarkerException : public CorruptMarkerException
{
};

} // namespace grk
