/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
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

#include <cstdint>
#include <atomic>

namespace io
{

class RefCounted
{
	friend class RefReaper;

  public:
	RefCounted(void) : refCount_(1) {}
	uint32_t ref(void)
	{
		return ++refCount_;
	}

  protected:
	virtual ~RefCounted() = default;

  private:
	uint32_t unref(void)
	{
		assert(refCount_ > 0);
		return --refCount_;
	}
	std::atomic<uint32_t> refCount_;
};

class RefReaper
{
  public:
	static void unref(RefCounted* refCounted)
	{
		if(!refCounted)
			return;
		if(refCounted->unref() == 0)
			delete refCounted;
	}
};

} // namespace io
