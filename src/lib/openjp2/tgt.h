/*
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
*
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

/**
@file tgt.h
@brief Implementation of a tag-tree coder (TGT)

The functions in TGT.C have for goal to realize a tag-tree coder. The functions in TGT.C
are used by some function in T2.C.
*/

/** @defgroup TGT TGT - Implementation of a tag-tree coder */
/*@{*/

/**
Tag node
*/
typedef struct opj_tgt_node {
    struct opj_tgt_node *parent;
    int32_t value;
    int32_t low;
    uint32_t known;
} opj_tgt_node_t;

/**
Tag tree
*/
typedef struct opj_tgt_tree {
    uint32_t  numleafsh;
    uint32_t  numleafsv;
    uint32_t numnodes;
    opj_tgt_node_t *nodes;
    uint32_t  nodes_size;		/* maximum size taken by nodes */
} opj_tgt_tree_t;


/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */
/**
Create a tag-tree
@param numleafsh Width of the array of leafs of the tree
@param numleafsv Height of the array of leafs of the tree
@return Returns a new tag-tree if successful, returns NULL otherwise
*/
opj_tgt_tree_t *opj_tgt_create(uint32_t numleafsh, uint32_t numleafsv, opj_event_mgr_t *manager);

/**
 * Reinitialises a tag-tree from an exixting one.
 *
 * @param	p_tree				the tree to reinitialize.
 * @param	p_num_leafs_h		the width of the array of leafs of the tree
 * @param	p_num_leafs_v		the height of the array of leafs of the tree
 * @param p_manager       the event manager
 * @return	a new tag-tree if successful, NULL otherwise
*/
opj_tgt_tree_t *opj_tgt_init(opj_tgt_tree_t * p_tree,
                             uint32_t  p_num_leafs_h,
                             uint32_t  p_num_leafs_v, opj_event_mgr_t *p_manager);
/**
Destroy a tag-tree, liberating memory
@param tree Tag-tree to destroy
*/
void opj_tgt_destroy(opj_tgt_tree_t *tree);
/**
Reset a tag-tree (set all leaves to 0)
@param tree Tag-tree to reset
*/
void opj_tgt_reset(opj_tgt_tree_t *tree);
/**
Set the value of a leaf of a tag-tree
@param tree Tag-tree to modify
@param leafno Number that identifies the leaf to modify
@param value New value of the leaf
*/
void opj_tgt_setvalue(opj_tgt_tree_t *tree,
                      uint32_t leafno,
                      int32_t value);
/**
Encode the value of a leaf of the tag-tree up to a given threshold
@param bio Pointer to a BIO handle
@param tree Tag-tree to modify
@param leafno Number that identifies the leaf to encode
@param threshold Threshold to use when encoding value of the leaf
*/
void opj_tgt_encode(BitIO *bio,
                    opj_tgt_tree_t *tree,
                    uint32_t leafno,
                    int32_t threshold);
/**
Decode the value of a leaf of the tag-tree up to a given threshold
@param bio Pointer to a BIO handle
@param tree Tag-tree to decode
@param leafno Number that identifies the leaf to decode
@param threshold Threshold to use when decoding value of the leaf
@return Returns 1 if the node's value < threshold, returns 0 otherwise
*/
uint32_t opj_tgt_decode(BitIO *bio,
                        opj_tgt_tree_t *tree,
                        uint32_t leafno,
                        int32_t threshold);
/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/


