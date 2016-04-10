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


/*
Vector - a dynamic array.

*/

typedef struct opj_vec {
    void* data;		/* array of void* pointers */
    bool owns_data;
} opj_vec_t;

/*
Initialize vector
*/

bool opj_vec_init(opj_vec_t *vec, bool owns_data);


/*
Add a value to the end of the vector
*/
bool opj_vec_push_back(opj_vec_t *vec, void* value);

/*
Get value at specified index
*/
void* opj_vec_get(opj_vec_t *vec, size_t index);

int32_t opj_vec_size(opj_vec_t *vec);

/*
Get value at end of vector
*/
void* opj_vec_back(opj_vec_t *vec);

/*
Clean up vector resources. Does NOT free vector itself
*/
void opj_vec_cleanup(opj_vec_t *vec);

/*
Clean up vector resources and free vector itself
*/
void opj_vec_destroy(opj_vec_t *vec);


