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
 */

#pragma once

namespace grk
{
enum GrkCacheState
{
   GRK_CACHE_STATE_CLOSED,
   GRK_CACHE_STATE_OPEN,
   GRK_CACHE_STATE_ERROR
};

class ICacheable
{
 public:
   ICacheable() : state_(GRK_CACHE_STATE_CLOSED) {}
   virtual ~ICacheable() = default;
   bool isOpen(void)
   {
	  return state_ == GRK_CACHE_STATE_OPEN;
   }
   bool isClosed(void)
   {
	  return state_ == GRK_CACHE_STATE_CLOSED;
   }
   bool isError(void)
   {
	  return state_ == GRK_CACHE_STATE_ERROR;
   }
   void setCacheState(GrkCacheState state)
   {
	  state_ = state;
   }

 private:
   GrkCacheState state_;
};

} // namespace grk
