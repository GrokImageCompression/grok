/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 * Translation helpers between grok's grk_* types and the GPU plugin's
 * gpup_* types.  Called once per encode/decode invocation — no pixel data
 * is copied; images and tiles are always shared via pointer.
 */

#pragma once

#include "grok.h"
#define GPUP_TYPES_ONLY
#include "gpup/gpu_plugin_shared.h"
#undef GPUP_TYPES_ONLY
#include <cstring>

namespace grk
{

/* ── grk_cparameters → gpup_compress_params ─────────────────── */
inline void grk_to_gpup_compress_params(const grk_cparameters* src, gpup_compress_params* dst)
{
  memset(dst, 0, sizeof(*dst));
  dst->tile_size_on = src->tile_size_on;
  dst->tx0 = src->tx0;
  dst->ty0 = src->ty0;
  dst->t_width = src->t_width;
  dst->t_height = src->t_height;
  dst->numlayers = src->numlayers;
  dst->allocationByRateDistoration = src->allocation_by_rate_distortion;
  for(uint16_t i = 0; i < src->numlayers && i < GPUP_MAX_LAYERS; ++i)
  {
    dst->layer_rate[i] = src->layer_rate[i];
    dst->layer_distortion[i] = src->layer_distortion[i];
  }
  dst->allocationByQuality = src->allocation_by_quality;
  dst->csty = src->csty;
  dst->numgbits = src->numgbits;
  dst->prog_order = (gpup_prog_order)src->prog_order;
  dst->numpocs = src->numpocs;
  dst->numresolution = src->numresolution;
  dst->cblockw_init = src->cblockw_init;
  dst->cblockh_init = src->cblockh_init;
  dst->cblk_sty = src->cblk_sty;
  dst->irreversible = src->irreversible;
  dst->roi_compno = src->roi_compno;
  dst->roi_shift = src->roi_shift;
  dst->res_spec = src->res_spec;
  for(uint32_t i = 0; i < GPUP_MAXRLVLS; ++i)
  {
    dst->prcw_init[i] = src->prcw_init[i];
    dst->prch_init[i] = src->prch_init[i];
  }
  memcpy(dst->infile, src->infile, GPUP_PATH_LEN);
  memcpy(dst->outfile, src->outfile, GPUP_PATH_LEN);
  dst->image_offset_x0 = src->image_offset_x0;
  dst->image_offset_y0 = src->image_offset_y0;
  dst->subsampling_dx = src->subsampling_dx;
  dst->subsampling_dy = src->subsampling_dy;
  dst->decod_format = (gpup_file_fmt)src->decod_format;
  dst->cod_format = (gpup_file_fmt)src->cod_format;
  dst->enableTilePartGeneration = src->enable_tile_part_generation;
  dst->newTilePartProgressionDivider = src->new_tile_part_progression_divider;
  dst->mct = src->mct;
  dst->max_cs_size = src->max_cs_size;
  dst->max_comp_size = src->max_comp_size;
  dst->rsiz = src->rsiz;
  dst->framerate = src->framerate;
  dst->rateControlAlgorithm = (gpup_rate_control)src->rate_control_algorithm;
  dst->numThreads = src->num_threads;
  dst->deviceId = src->device_id;
  dst->duration = src->duration;
  dst->kernelBuildOptions = src->kernel_build_options;
  dst->repeats = src->repeats;
  dst->verbose = false;
  dst->sharedMemoryInterface = src->shared_memory_interface;
}

/* ── grk_decompress_parameters → gpup_decompress_params ─────── */
inline void grk_to_gpup_decompress_params(const grk_decompress_parameters* src,
                                          gpup_decompress_params* dst)
{
  memset(dst, 0, sizeof(*dst));
  dst->core.reduce = src->core.reduce;
  dst->core.layers_to_decompress_ = src->core.layers_to_decompress;
  memcpy(dst->infile, src->infile, GPUP_PATH_LEN);
  memcpy(dst->outfile, src->outfile, GPUP_PATH_LEN);
  dst->decod_format = (gpup_codec_fmt)src->decod_format;
  dst->cod_format = (gpup_file_fmt)src->cod_format;
  dst->dw_x0 = src->dw_x0;
  dst->dw_y0 = src->dw_y0;
  dst->dw_x1 = src->dw_x1;
  dst->dw_y1 = src->dw_y1;
  dst->tileIndex = src->tile_index;
  dst->deviceId = src->device_id;
  dst->kernelBuildOptions = src->kernel_build_options;
  dst->repeats = src->repeats;
  dst->numThreads = src->num_threads;
  dst->verbose_ = false;
  dst->user_data = src->user_data;
}

/* ── gpup_header_info → grk_header_info (fields the plugin fills in) ── */
inline void gpup_to_grk_header_info(const gpup_header_info* src, grk_header_info* dst)
{
  dst->cblockw_init = src->cblockw_init;
  dst->cblockh_init = src->cblockh_init;
  dst->irreversible = src->irreversible;
  dst->mct = src->mct;
  dst->rsiz = src->rsiz;
  dst->numresolutions = src->numresolutions;
  dst->csty = src->csty;
  dst->cblk_sty = src->cblk_sty;
  for(uint32_t i = 0; i < GPUP_MAXRLVLS; ++i)
  {
    dst->prcw_init[i] = src->prcw_init[i];
    dst->prch_init[i] = src->prch_init[i];
  }
  dst->tx0 = src->tx0;
  dst->ty0 = src->ty0;
  dst->t_width = src->t_width;
  dst->t_height = src->t_height;
  dst->t_grid_width = src->t_grid_width;
  dst->t_grid_height = src->t_grid_height;
  dst->num_layers = src->max_layers_;
}

/* ── grk_header_info → gpup_header_info ───────────────────────── */
inline void grk_to_gpup_header_info(const grk_header_info* src, gpup_header_info* dst)
{
  memset(dst, 0, sizeof(*dst));
  dst->cblockw_init = src->cblockw_init;
  dst->cblockh_init = src->cblockh_init;
  dst->irreversible = src->irreversible;
  dst->mct = src->mct;
  dst->rsiz = src->rsiz;
  dst->numresolutions = src->numresolutions;
  dst->csty = src->csty;
  dst->cblk_sty = src->cblk_sty;
  for(uint32_t i = 0; i < GPUP_MAXRLVLS; ++i)
  {
    dst->prcw_init[i] = src->prcw_init[i];
    dst->prch_init[i] = src->prch_init[i];
  }
  dst->tx0 = src->tx0;
  dst->ty0 = src->ty0;
  dst->t_width = src->t_width;
  dst->t_height = src->t_height;
  dst->t_grid_width = src->t_grid_width;
  dst->t_grid_height = src->t_grid_height;
  dst->max_layers_ = src->num_layers;
}

/* ── grk_image → gpup_image (shallow: shares component data pointers) ── */
inline gpup_image* grk_to_gpup_image(const grk_image* src)
{
  if(!src)
    return nullptr;
  uint16_t n = src->numcomps > 16 ? 16 : src->numcomps;
  auto* img = new gpup_image();
  memset(img, 0, sizeof(gpup_image));
  img->x0 = src->x0;
  img->y0 = src->y0;
  img->x1 = src->x1;
  img->y1 = src->y1;
  img->numcomps = n;
  img->color_space = (gpup_color_space)src->color_space;
  img->comps = new gpup_image_comp[n]();
  for(uint16_t c = 0; c < n; ++c)
  {
    img->comps[c].x0 = src->comps[c].x0;
    img->comps[c].y0 = src->comps[c].y0;
    img->comps[c].w = src->comps[c].w;
    img->comps[c].stride = src->comps[c].stride;
    img->comps[c].h = src->comps[c].h;
    img->comps[c].dx = src->comps[c].dx;
    img->comps[c].dy = src->comps[c].dy;
    img->comps[c].prec = src->comps[c].prec;
    img->comps[c].sgnd = src->comps[c].sgnd;
    img->comps[c].data = (int32_t*)src->comps[c].data;
    img->comps[c].owns_data = false; // host owns the data
  }
  return img;
}

/* Free a gpup_image created by grk_to_gpup_image (does NOT free component data) */
inline void gpup_image_free_shell(gpup_image* img)
{
  if(!img)
    return;
  delete[] img->comps;
  delete img;
}

/* ── gpup_image → grk_image (shallow: shares component data pointers) ── */
/* NOTE: the returned image has a zeroed grk_object; only data pointers are valid. */
inline void gpup_to_grk_image_shallow(const gpup_image* src, grk_image* dst)
{
  if(!src)
    return;
  memset(dst, 0, sizeof(*dst));
  dst->x0 = src->x0;
  dst->y0 = src->y0;
  dst->x1 = src->x1;
  dst->y1 = src->y1;
  dst->numcomps = src->numcomps;
  dst->color_space = (GRK_COLOR_SPACE)src->color_space;
  // comps must be allocated by caller
}

/* ── gpup_tile → grk_plugin_tile (deep wrapper — shares data pointers) ── */
/*
 * Creates a grk_plugin_tile tree that mirrors a gpup_tile tree.
 * All data buffers (compressedData, passes, contextStream) are SHARED —
 * only the tree scaffolding is newly allocated.
 * Caller must free with grk_plugin_tile_free_wrapper().
 */
inline grk_plugin_tile* gpup_tile_to_grk(gpup_tile* src)
{
  if(!src)
    return nullptr;
  auto* dst = new grk_plugin_tile();
  dst->decompress_flags = src->decompress_flags;
  dst->num_components = (uint16_t)src->numComponents;
  dst->tile_components = new grk_plugin_tile_component*[src->numComponents]();
  for(size_t c = 0; c < src->numComponents; ++c)
  {
    auto* sc = src->tileComponents[c];
    auto* dc = new grk_plugin_tile_component();
    dc->numresolutions = (uint8_t)sc->numResolutions;
    dc->resolutions = new grk_plugin_resolution*[sc->numResolutions]();
    for(size_t r = 0; r < sc->numResolutions; ++r)
    {
      auto* sr = sc->resolutions[r];
      auto* dr = new grk_plugin_resolution();
      dr->level = (uint8_t)sr->level;
      dr->num_bands = (uint8_t)sr->numBands;
      dr->band = new grk_plugin_band*[sr->numBands]();
      for(size_t b = 0; b < sr->numBands; ++b)
      {
        auto* sb = sr->band[b];
        auto* db = new grk_plugin_band();
        db->orientation = sb->orientation;
        db->num_precincts = sb->numPrecincts;
        db->precincts = new grk_plugin_precinct*[sb->numPrecincts]();
        db->stepsize = sb->stepsize;
        for(uint64_t p = 0; p < sb->numPrecincts; ++p)
        {
          auto* sp = sb->precincts[p];
          auto* dp = new grk_plugin_precinct();
          dp->num_blocks = sp->numBlocks;
          dp->blocks = new grk_plugin_code_block*[sp->numBlocks]();
          for(uint64_t k = 0; k < sp->numBlocks; ++k)
          {
            auto* skb = sp->blocks[k];
            auto* dkb = new grk_plugin_code_block();
            dkb->x0 = skb->x0;
            dkb->y0 = skb->y0;
            dkb->x1 = skb->x1;
            dkb->y1 = skb->y1;
            dkb->context_stream = skb->contextStream; // shared
            dkb->num_pix = skb->numPix;
            dkb->compressed_data = skb->compressedData; // shared
            dkb->compressed_data_length = skb->compressedDataLength;
            dkb->num_bit_planes = skb->numBitPlanes;
            dkb->num_passes = (uint8_t)skb->numPasses;
            for(size_t ps = 0; ps < skb->numPasses && ps < GRK_MAX_PASSES; ++ps)
            {
              dkb->passes[ps].distortion_decrease = skb->passes[ps].distortionDecrease;
              dkb->passes[ps].rate = skb->passes[ps].rate;
              dkb->passes[ps].length = skb->passes[ps].length;
            }
            dkb->sorted_index = skb->sortedIndex;
            dp->blocks[k] = dkb;
          }
          db->precincts[p] = dp;
        }
        dr->band[b] = db;
      }
      dc->resolutions[r] = dr;
    }
    dst->tile_components[c] = dc;
  }
  return dst;
}

/* ── Fast update: reuse existing wrapper tree, just update shared data pointers ──
 * All tiles in a batch have identical structure (same image dimensions).
 * Instead of allocating a new wrapper tree for each tile, we reuse the
 * existing one and update only the data pointers that change per tile.
 */
inline void gpup_tile_update_grk(grk_plugin_tile* dst, gpup_tile* src)
{
  if(!dst || !src)
    return;
  dst->decompress_flags = src->decompress_flags;
  uint16_t nc = std::min(dst->num_components, (uint16_t)src->numComponents);
  for(uint16_t c = 0; c < nc; ++c)
  {
    auto* dc = dst->tile_components[c];
    auto* sc = src->tileComponents[c];
    if(!dc || !sc)
      continue;
    uint8_t nr = std::min(dc->numresolutions, (uint8_t)sc->numResolutions);
    for(uint8_t r = 0; r < nr; ++r)
    {
      auto* dr = dc->resolutions[r];
      auto* sr = sc->resolutions[r];
      if(!dr || !sr)
        continue;
      uint8_t nb = std::min(dr->num_bands, (uint8_t)sr->numBands);
      for(uint8_t b = 0; b < nb; ++b)
      {
        auto* db = dr->band[b];
        auto* sb = sr->band[b];
        if(!db || !sb)
          continue;
        db->stepsize = sb->stepsize;
        uint64_t np = std::min(db->num_precincts, sb->numPrecincts);
        for(uint64_t p = 0; p < np; ++p)
        {
          auto* dp = db->precincts[p];
          auto* sp = sb->precincts[p];
          if(!dp || !sp)
            continue;
          uint64_t nk = std::min(dp->num_blocks, sp->numBlocks);
          for(uint64_t k = 0; k < nk; ++k)
          {
            auto* dkb = dp->blocks[k];
            auto* skb = sp->blocks[k];
            if(!dkb || !skb)
              continue;
            dkb->context_stream = skb->contextStream;
            dkb->compressed_data = skb->compressedData;
            dkb->compressed_data_length = skb->compressedDataLength;
            dkb->num_bit_planes = skb->numBitPlanes;
            dkb->num_passes = (uint8_t)skb->numPasses;
          }
        }
      }
    }
  }
}

/* ── Sync codeblock metadata from grk_plugin_tile wrapper back to original gpup_tile ──
 * After T2 decompress, the host fills wrapper codeblock metadata (num_passes,
 * num_bit_planes, compressed_data_length).  The compressed data buffer is shared,
 * but the metadata fields are separate copies.  This function syncs them back so
 * the GPU plugin's transferDecodeInfo() sees the updated values.
 */
inline void grk_tile_sync_metadata_to_gpup(grk_plugin_tile* wrapper, gpup_tile* original)
{
  if(!wrapper || !original)
    return;
  uint16_t nc = std::min(wrapper->num_components, (uint16_t)original->numComponents);
  for(uint16_t c = 0; c < nc; ++c)
  {
    auto* wc = wrapper->tile_components[c];
    auto* oc = original->tileComponents[c];
    if(!wc || !oc)
      continue;
    uint8_t nr = std::min(wc->numresolutions, (uint8_t)oc->numResolutions);
    for(uint8_t r = 0; r < nr; ++r)
    {
      auto* wr = wc->resolutions[r];
      auto* or_ = oc->resolutions[r];
      if(!wr || !or_)
        continue;
      uint8_t nb = std::min(wr->num_bands, (uint8_t)or_->numBands);
      for(uint8_t b = 0; b < nb; ++b)
      {
        auto* wb = wr->band[b];
        auto* ob = or_->band[b];
        if(!wb || !ob)
          continue;
        // sync band-level stepsize (host sets it in decompress_synch_plugin_with_host)
        ob->stepsize = wb->stepsize;
        uint64_t np = std::min(wb->num_precincts, ob->numPrecincts);
        for(uint64_t p = 0; p < np; ++p)
        {
          auto* wp = wb->precincts[p];
          auto* op = ob->precincts[p];
          if(!wp || !op)
            continue;
          uint64_t nk = std::min(wp->num_blocks, op->numBlocks);
          for(uint64_t k = 0; k < nk; ++k)
          {
            auto* wkb = wp->blocks[k];
            auto* okb = op->blocks[k];
            if(!wkb || !okb)
              continue;
            okb->compressedDataLength = wkb->compressed_data_length;
            okb->numBitPlanes = wkb->num_bit_planes;
            okb->numPasses = wkb->num_passes;
          }
        }
      }
    }
  }
}

/* Free the wrapper tree created by gpup_tile_to_grk (does NOT free shared data) */
inline void grk_plugin_tile_free_wrapper(grk_plugin_tile* t)
{
  if(!t)
    return;
  for(uint16_t c = 0; c < t->num_components; ++c)
  {
    auto* tc = t->tile_components[c];
    if(!tc)
      continue;
    for(uint8_t r = 0; r < tc->numresolutions; ++r)
    {
      auto* res = tc->resolutions[r];
      if(!res)
        continue;
      for(uint8_t b = 0; b < res->num_bands; ++b)
      {
        auto* band = res->band[b];
        if(!band)
          continue;
        for(uint64_t p = 0; p < band->num_precincts; ++p)
        {
          auto* prc = band->precincts[p];
          if(!prc)
            continue;
          for(uint64_t k = 0; k < prc->num_blocks; ++k)
            delete prc->blocks[k];
          delete[] prc->blocks;
          delete prc;
        }
        delete[] band->precincts;
        delete band;
      }
      delete[] res->band;
      delete res;
    }
    delete[] tc->resolutions;
    delete tc;
  }
  delete[] t->tile_components;
  delete t;
}

/* ── grk_plugin_tile → gpup_tile (for encode: sync data back to plugin) ── */
inline gpup_tile* grk_tile_to_gpup(grk_plugin_tile* src)
{
  if(!src)
    return nullptr;
  auto* dst = new gpup_tile();
  dst->decompress_flags = src->decompress_flags;
  dst->numComponents = src->num_components;
  dst->tileComponents = new gpup_tile_component*[src->num_components]();
  for(uint16_t c = 0; c < src->num_components; ++c)
  {
    auto* sc = src->tile_components[c];
    auto* dc = new gpup_tile_component();
    dc->numResolutions = sc->numresolutions;
    dc->resolutions = new gpup_resolution*[sc->numresolutions]();
    for(uint8_t r = 0; r < sc->numresolutions; ++r)
    {
      auto* sr = sc->resolutions[r];
      auto* dr = new gpup_resolution();
      dr->level = sr->level;
      dr->numBands = sr->num_bands;
      dr->band = new gpup_band*[sr->num_bands]();
      for(uint8_t b = 0; b < sr->num_bands; ++b)
      {
        auto* sb = sr->band[b];
        auto* db = new gpup_band();
        db->orientation = sb->orientation;
        db->numPrecincts = sb->num_precincts;
        db->precincts = new gpup_precinct*[sb->num_precincts]();
        db->stepsize = sb->stepsize;
        for(uint64_t p = 0; p < sb->num_precincts; ++p)
        {
          auto* sp = sb->precincts[p];
          auto* dp = new gpup_precinct();
          dp->numBlocks = sp->num_blocks;
          dp->blocks = new gpup_code_block*[sp->num_blocks]();
          for(uint64_t k = 0; k < sp->num_blocks; ++k)
          {
            auto* skb = sp->blocks[k];
            auto* dkb = new gpup_code_block();
            dkb->x0 = skb->x0;
            dkb->y0 = skb->y0;
            dkb->x1 = skb->x1;
            dkb->y1 = skb->y1;
            dkb->contextStream = skb->context_stream;
            dkb->numPix = skb->num_pix;
            dkb->compressedData = skb->compressed_data;
            dkb->compressedDataLength = skb->compressed_data_length;
            dkb->numBitPlanes = skb->num_bit_planes;
            dkb->numPasses = skb->num_passes;
            for(uint8_t ps = 0; ps < skb->num_passes && ps < GRK_MAX_PASSES; ++ps)
            {
              dkb->passes[ps].distortionDecrease = skb->passes[ps].distortion_decrease;
              dkb->passes[ps].rate = skb->passes[ps].rate;
              dkb->passes[ps].length = skb->passes[ps].length;
            }
            dkb->sortedIndex = skb->sorted_index;
            dp->blocks[k] = dkb;
          }
          db->precincts[p] = dp;
        }
        dr->band[b] = db;
      }
      dc->resolutions[r] = dr;
    }
    dst->tileComponents[c] = dc;
  }
  return dst;
}

/* Free a gpup_tile wrapper created by grk_tile_to_gpup (does NOT free shared data) */
inline void gpup_tile_free_wrapper(gpup_tile* t)
{
  if(!t)
    return;
  for(size_t c = 0; c < t->numComponents; ++c)
  {
    auto* tc = t->tileComponents[c];
    if(!tc)
      continue;
    for(size_t r = 0; r < tc->numResolutions; ++r)
    {
      auto* res = tc->resolutions[r];
      if(!res)
        continue;
      for(size_t b = 0; b < res->numBands; ++b)
      {
        auto* band = res->band[b];
        if(!band)
          continue;
        for(uint64_t p = 0; p < band->numPrecincts; ++p)
        {
          auto* prc = band->precincts[p];
          if(!prc)
            continue;
          for(uint64_t k = 0; k < prc->numBlocks; ++k)
            delete prc->blocks[k];
          delete[] prc->blocks;
          delete prc;
        }
        delete[] band->precincts;
        delete band;
      }
      delete[] res->band;
      delete res;
    }
    delete[] tc->resolutions;
    delete tc;
  }
  delete[] t->tileComponents;
  delete t;
}

/* ── grk_stream_params → gpup_stream_params ───────────────────── */
inline void grk_to_gpup_stream_params(const grk_stream_params* src, gpup_stream_params* dst)
{
  memset(dst, 0, sizeof(*dst));
  dst->file = src->file;
  dst->buf = src->buf;
  dst->buf_len = src->buf_len;
  dst->buf_compressed_len = src->buf_compressed_len;
}
inline void gpup_to_grk_stream_params(const gpup_stream_params* src, grk_stream_params* dst)
{
  memset(dst, 0, sizeof(*dst));
  if(src->file)
    strncpy(dst->file, src->file, GRK_PATH_LEN - 1);
  dst->buf = src->buf;
  dst->buf_len = src->buf_len;
  dst->buf_compressed_len = src->buf_compressed_len;
}

} // namespace grk
