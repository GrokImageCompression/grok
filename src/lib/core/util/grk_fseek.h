/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
 */

#pragma once

/*
 Use fseeko() and ftello() if they are available since they use
 'int64_t' rather than 'long'.  It is wrong to use fseeko() and
 ftello() only on systems with special LFS support since some systems
 (e.g. FreeBSD) support a 64-bit int64_t by default.
 */
#if defined(GROK_HAVE_FSEEKO) && !defined(fseek)
#define fseek fseeko
#define ftell ftello
#endif
#if defined(_WIN32)
#define GRK_FSEEK(stream, offset, whence) _fseeki64(stream, /* __int64 */ offset, whence)
#define GRK_FTELL(stream) /* __int64 */ _ftelli64(stream)
#else
#define GRK_FSEEK(stream, offset, whence) fseek(stream, offset, whence)
#define GRK_FTELL(stream) ftell(stream)
#endif
