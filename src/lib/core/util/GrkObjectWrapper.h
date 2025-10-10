/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include "grok.h"

#include <cassert>
#include <cstdint>
#include "RefCounted.h"

namespace grk
{

template<typename T>
class GrkObjectWrapperImpl : public RefCounted
{
public:
  explicit GrkObjectWrapperImpl(T* wrap) : wrappee(wrap) {}
  T* getWrappee(void)
  {
    assert(wrappee);
    return wrappee;
  }

protected:
  virtual ~GrkObjectWrapperImpl(void) override
  {
    delete wrappee;
    wrappee = nullptr;
  }

private:
  T* wrappee;
};

} // namespace grk
