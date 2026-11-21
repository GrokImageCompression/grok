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

#include <cstdio>

#include "grok.h"

namespace
{
bool expectWorkers(size_t expected, const char* step)
{
  size_t workers = grk_num_workers();
  if(workers == expected)
    return true;
  fprintf(stderr, "%s: %zu workers, expected %zu\n", step, workers, expected);
  return false;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);
  size_t defaultWorkers = grk_num_workers();
  grk_deinitialize();
  if(defaultWorkers == 0)
  {
    fprintf(stderr, "default initialize reports no workers\n");
    return 1;
  }

  size_t explicitWorkers = defaultWorkers + 1;
  grk_initialize(nullptr, (uint32_t)explicitWorkers, nullptr);
  bool ok = expectWorkers(explicitWorkers, "initialize after deinitialize with an explicit count");
  grk_deinitialize();
  if(!ok)
    return 1;

  // the codec entry points initialize with UINT32_MAX, which must rebuild the
  // default pool once the library has been deinitialized
  grk_initialize(nullptr, UINT32_MAX, nullptr);
  ok = expectWorkers(defaultWorkers, "auto initialize after deinitialize");
  grk_deinitialize();
  return ok ? 0 : 1;
}
