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
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>

#include "grok.h"

static void printUsage(const char* prog)
{
  fprintf(stderr,
          "Usage: %s -i <input.jp2> -o <output.jp2> [options]\n"
          "\n"
          "Transcode a JPEG 2000 file (JP2), rewriting boxes and optionally\n"
          "modifying the codestream without full decompression.\n"
          "\n"
          "Options:\n"
          "  -i, --input <file>       Input JP2 file (required)\n"
          "  -o, --output <file>      Output JP2 file (required)\n"
          "  -X, --tlm               Insert TLM markers\n"
          "  -L, --plt               Insert PLT markers\n"
          "  -S, --sop               Inject SOP markers before each packet\n"
          "  -E, --eph               Inject EPH markers after each packet header\n"
          "  -n, --max-layers <N>    Keep at most N quality layers (0 = all)\n"
          "  -R, --max-res <N>       Keep at most N resolution levels (0 = all)\n"
          "  -p, --progression <P>   Reorder to progression P\n"
          "                          (LRCP, RLCP, RPCL, PCRL, CPRL)\n"
          "  -h, --help              Print this help message\n"
          "  -v, --version           Print library version\n",
          prog);
}

static GRK_PROG_ORDER parseProgOrder(const char* str)
{
  if(strcmp(str, "LRCP") == 0)
    return GRK_LRCP;
  if(strcmp(str, "RLCP") == 0)
    return GRK_RLCP;
  if(strcmp(str, "RPCL") == 0)
    return GRK_RPCL;
  if(strcmp(str, "PCRL") == 0)
    return GRK_PCRL;
  if(strcmp(str, "CPRL") == 0)
    return GRK_CPRL;
  return GRK_PROG_UNKNOWN;
}

template<size_t N>
static void safe_strcpy(char (&dest)[N], const char* src)
{
  size_t len = strnlen(src, N - 1);
  memcpy(dest, src, len);
  dest[len] = '\0';
}

int main(int argc, char* argv[])
{
  const char* inputFile = nullptr;
  const char* outputFile = nullptr;
  bool writeTlm = false;
  bool writePlt = false;
  bool writeSop = false;
  bool writeEph = false;
  uint16_t maxLayers = 0;
  uint8_t maxRes = 0;
  GRK_PROG_ORDER progOrder = GRK_PROG_UNKNOWN;

  for(int i = 1; i < argc; ++i)
  {
    if((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) && i + 1 < argc)
      inputFile = argv[++i];
    else if((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc)
      outputFile = argv[++i];
    else if(strcmp(argv[i], "-X") == 0 || strcmp(argv[i], "--tlm") == 0)
      writeTlm = true;
    else if(strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--plt") == 0)
      writePlt = true;
    else if(strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--sop") == 0)
      writeSop = true;
    else if(strcmp(argv[i], "-E") == 0 || strcmp(argv[i], "--eph") == 0)
      writeEph = true;
    else if((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--max-layers") == 0) && i + 1 < argc)
      maxLayers = (uint16_t)atoi(argv[++i]);
    else if((strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--max-res") == 0) && i + 1 < argc)
      maxRes = (uint8_t)atoi(argv[++i]);
    else if((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--progression") == 0) && i + 1 < argc)
    {
      progOrder = parseProgOrder(argv[++i]);
      if(progOrder == GRK_PROG_UNKNOWN)
      {
        fprintf(stderr, "Error: unknown progression order '%s'\n", argv[i]);
        return 1;
      }
    }
    else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
    {
      printUsage(argv[0]);
      return 0;
    }
    else if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
    {
      fprintf(stdout, "grk_transcode using Grok library %s\n", grk_version());
      return 0;
    }
    else
    {
      fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
      printUsage(argv[0]);
      return 1;
    }
  }

  if(!inputFile || !outputFile)
  {
    fprintf(stderr, "Error: input (-i) and output (-o) files are required\n");
    printUsage(argv[0]);
    return 1;
  }

  grk_initialize(nullptr, 0, nullptr);

  /* Step 1: Decompress header from source to get image info */
  grk_stream_params srcStreamParams{};
  safe_strcpy(srcStreamParams.file, inputFile);

  grk_decompress_parameters dparams{};
  auto* decCodec = grk_decompress_init(&srcStreamParams, &dparams);
  if(!decCodec)
  {
    fprintf(stderr, "Error: failed to init decompressor for '%s'\n", inputFile);

    return 1;
  }

  grk_header_info srcHeader{};
  if(!grk_decompress_read_header(decCodec, &srcHeader))
  {
    fprintf(stderr, "Error: failed to read header from '%s'\n", inputFile);
    grk_object_unref(decCodec);

    return 1;
  }

  auto* srcImage = grk_decompress_get_image(decCodec);
  if(!srcImage)
  {
    fprintf(stderr, "Error: failed to get image from '%s'\n", inputFile);
    grk_object_unref(decCodec);

    return 1;
  }

  /* Step 2: Set up transcode parameters */
  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_JP2;
  cparams.write_tlm = writeTlm;
  cparams.write_plt = writePlt;
  cparams.write_sop = writeSop;
  cparams.write_eph = writeEph;
  cparams.max_layers_transcode = maxLayers;
  cparams.max_res_transcode = maxRes;
  cparams.transcode_prog_order = progOrder;

  grk_stream_params dstStreamParams{};
  safe_strcpy(dstStreamParams.file, outputFile);

  grk_stream_params transSrcStreamParams{};
  safe_strcpy(transSrcStreamParams.file, inputFile);

  /* Step 3: Transcode */
  uint64_t written = grk_transcode(&transSrcStreamParams, &dstStreamParams, &cparams, srcImage);

  grk_object_unref(decCodec);

  if(written == 0)
  {
    fprintf(stderr, "Error: transcode failed\n");
    return 1;
  }

  fprintf(stdout, "Transcoded %s -> %s (%" PRIu64 " bytes written)\n", inputFile, outputFile,
          written);
  return 0;
}
