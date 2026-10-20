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
#include <cstring>

#include "grok.h"

namespace
{
const char* MISSING_PLUGIN_DIRECTORY = "grk_initialize_plugin_path_test_missing_directory";
const uint32_t NUM_THREADS = 2;

char* heapCopy(const char* text)
{
  auto copy = new char[std::strlen(text) + 1];
  std::strcpy(copy, text);
  return copy;
}
} // namespace

int main(void)
{
  bool pluginInitialised = true;
  char* firstPath = heapCopy(MISSING_PLUGIN_DIRECTORY);
  grk_initialize(firstPath, NUM_THREADS, &pluginInitialised);
  delete[] firstPath;
  if(pluginInitialised)
  {
    fprintf(stderr, "first grk_initialize loaded a plugin from a missing directory\n");
    grk_deinitialize();
    return 1;
  }

  pluginInitialised = true;
  char* secondPath = heapCopy(MISSING_PLUGIN_DIRECTORY);
  grk_initialize(secondPath, NUM_THREADS, &pluginInitialised);
  delete[] secondPath;
  grk_deinitialize();
  if(pluginInitialised)
  {
    fprintf(stderr, "second grk_initialize reports a plugin from a missing directory\n");
    return 1;
  }
  return 0;
}
