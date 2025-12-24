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

#include <atomic>
#include <cstdint>
#include <stdexcept>

#include "Logger.h"
#include "grok.h"

namespace grk
{

template<typename T>
T* grk_unref(T* w)
{
  if(!w)
    return nullptr;
  grk_object_unref(&w->obj);
  return w;
}
template<typename T>
T* grk_ref(T* w)
{
  if(!w)
    return nullptr;
  grk_object_ref(&w->obj);
  return w;
}

struct NoopDeleter
{
  void operator()(void* obj) const
  {
    (void)obj;
  }
};

template<typename T>
struct RefCountedDeleter
{
  void operator()(T* ptr) const
  {
    if(ptr)
    {
      grk_unref(ptr);
    }
  }
};

class RefCounted
{
public:
  RefCounted() : ref_count(1) {}
  RefCounted* ref()
  {
    ++ref_count;
    return this;
  }

  void unref()
  {
    if(ref_count == 0)
    {
      grklog.warn("Attempt to unref a released object");
      throw std::runtime_error("Attempt to unref a released object");
    }
    uint32_t r = --ref_count;
    if(r == 0)
    {
      delete this;
    }
  }
  // Delete copy constructor and assignment operator
  RefCounted(const RefCounted&) = delete;
  RefCounted& operator=(const RefCounted&) = delete;

protected:
  virtual ~RefCounted() = default;

private:
  std::atomic<uint32_t> ref_count;
};

} // namespace grk
