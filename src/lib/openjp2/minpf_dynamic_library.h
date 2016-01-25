/**
*    Copyright (C) 2016 Grok Image Compression Inc.
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

#include "minpf_common.h"

typedef struct minpf_dynamic_library {

    char path[MINPF_MAX_PATH_LEN];
    void* handle;

} minpf_dynamic_library;

minpf_dynamic_library* minpf_load_dynamic_library(const char* path, char* error);
void* minpf_get_symbol(minpf_dynamic_library* library, const char* symbol);


