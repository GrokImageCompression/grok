/**
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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


#include "opj_includes.h"
#include <vector>

static std::vector<void*>* vec_impl(void* data)
{
    return (std::vector<void*>*)data;
}



bool opj_vec_init(opj_vec_t *vec, bool owns_data)
{
    if (!vec)
        return false;
    if (vec->data)
        return true;

    vec->data = (void*)(new std::vector<void*>());
    vec->owns_data = owns_data;
    return vec->data ? true : false;
}

bool opj_vec_push_back(opj_vec_t *vec, void* value)
{
    auto impl = vec_impl(vec->data);
    impl->push_back(value);
    return true;
}

void* opj_vec_get(opj_vec_t *vec, size_t index)
{
    if (!vec || !vec->data)
        return NULL;
    auto impl = vec_impl(vec->data);
    assert(index < impl->size() && index >= 0);
    if (index >= impl->size()) {
        return NULL;
    }
    return impl->operator[](index);
}

int32_t opj_vec_size(opj_vec_t *vec)
{
    if (!vec || !vec->data)
        return 0;
    auto impl = vec_impl(vec->data);
    return (int32_t)impl->size();
}

void* opj_vec_back(opj_vec_t *vec)
{
    if (!vec || !vec->data)
        return NULL;
    auto impl = vec_impl(vec->data);
    return impl->back();
}

void opj_vec_cleanup(opj_vec_t *vec)
{
    if (!vec || !vec->data)
        return;
    auto impl = vec_impl(vec->data);
    if (vec->owns_data) {
        for (auto it = impl->begin(); it != impl->end(); ++it) {
            if (*it)
                opj_free(*it);
        }
    }
    delete impl;
    vec->data = NULL;
}

void opj_vec_destroy(opj_vec_t *vec)
{
    if (!vec)
        return;
    opj_vec_cleanup(vec);
    opj_free(vec);
}