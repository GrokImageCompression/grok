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

namespace grk
{

template<typename T, size_t N>
struct vec
{
   vec(void) : val{0} {}
   explicit vec(T m)
   {
	  for(size_t i = 0; i < N; ++i)
		 val[i] = m;
   }
   vec operator+(const vec& rhs)
   {
	  vec rc;
	  for(size_t i = 0; i < N; ++i)
		 rc.val[i] = val[i] + rhs.val[i];

	  return rc;
   }
   vec& operator+=(const vec& rhs)
   {
	  for(size_t i = 0; i < N; ++i)
		 val[i] += rhs.val[i];

	  return *this;
   }
   vec operator-(const vec& rhs)
   {
	  vec rc;
	  for(size_t i = 0; i < N; ++i)
		 rc.val[i] = val[i] - rhs.val[i];

	  return rc;
   }
   vec& operator-=(const vec& rhs)
   {
	  for(size_t i = 0; i < N; ++i)
		 val[i] -= rhs.val[i];

	  return *this;
   }

   constexpr static size_t NUM_ELTS = N;
   T val[N];
};

} // namespace grk
