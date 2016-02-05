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
#ifdef WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include "opj_string.h"
#include "minpf_dynamic_library.h"

#ifdef _WIN32
typedef HMODULE dynamic_handle_t;
#else
typedef void* dynamic_handle_t;
#endif


minpf_dynamic_library*  minpf_load_dynamic_library(const char* path, char* error)
{

    minpf_dynamic_library* lib = NULL;
	dynamic_handle_t handle = NULL;

    if (!path)
        return NULL;

#ifdef _WIN32
    handle = LoadLibrary(path);
    if (handle == NULL) {
        return NULL;
    }
#else
    handle = dlopen(path, RTLD_NOW);
    if (!handle) {

        //report error
        return NULL;
    }

#endif

    lib = (minpf_dynamic_library*)calloc(1, sizeof(minpf_dynamic_library));
	if (!lib) {
#ifdef WIN32
		FreeLibrary(handle);
#else
		dlclose(handle);
#endif
		
		return NULL;

	}
	opj_strcpy_s(lib->path,sizeof(lib->path), path);
    lib->handle = handle;
    return lib;
}

void* minpf_get_symbol(minpf_dynamic_library* library, const char* symbol)
{

    if (!library || !library->handle)
        return NULL;

    void* rc = NULL;

#ifdef WIN32
    rc =  GetProcAddress((HMODULE)library->handle, symbol);
    if (!rc) {

        int err = GetLastError();
    }
#else
    rc =  dlsym(library->handle, symbol);
#endif

    return rc;

}
