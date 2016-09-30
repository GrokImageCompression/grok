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


#include "minpf_dynamic_library.h"

#include <string>

#ifdef _WIN32

//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
static std::string GetLastErrorAsString()
{
	//Get the error message, if any.
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return std::string(); //No error message has been recorded

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return message;
}

#endif


bool minpf_get_full_path(const char* path,
							void *addr,
							dynamic_handle_t handle,
							char* fullPath,
							size_t fullPathLen) {

	if (!path || !addr || !fullPath || !fullPathLen) {
		return false;
	}

#ifdef _WIN32
	auto dwLength = GetModuleFileName(handle, fullPath, (DWORD)fullPathLen);
	if (dwLength == 0) {
		auto error = GetLastErrorAsString();
		error = "GetModuleFileName failed: " + error + "\n";
		fprintf(stderr, "%s",error.c_str());
		return false;
	}
	return true;
#else
	Dl_info dl_info;
	memset(&dl_info, 0, sizeof(dl_info));
	if (dladdr(addr, &dl_info) && strlen(dl_info.dli_fname) < fullPathLen) {
		strcpy(fullPath, dl_info.dli_fname);
		return true;
	}
	fprintf(stderr, "dladdr failed\n");
	return false;

#endif
}


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
	strcpy_s(lib->path,sizeof(lib->path), path);
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
