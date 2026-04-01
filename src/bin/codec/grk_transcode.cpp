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

enum TranscodeFormat
{
  TFMT_CONTAINER, /* JP2 or JPH container */
  TFMT_CODESTREAM, /* raw codestream (j2k, j2c, jpc) */
  TFMT_UNKNOWN
};

static void printUsage(const char* prog)
{
  fprintf(stderr,
          "Usage: %s -i <input> -o <output> [options]\n"
          "\n"
          "Transcode a JPEG 2000 file, rewriting boxes/markers and optionally\n"
          "modifying the codestream without full decompression.\n"
          "Supported formats: JP2 (.jp2), JPH (.jph), J2K (.j2k, .j2c, .jpc).\n"
          "\n"
          "When the output format differs from the input, container-to-codestream\n"
          "conversion (JP2/JPH -> J2K) strips all box metadata. The reverse\n"
          "direction (J2K -> JP2/JPH) is not supported; use grk_compress instead.\n"
          "\n"
          "Options:\n"
          "  -i, --input <file>       Input file: JP2, JPH, or raw J2K/J2C/JPC (required)\n"
          "  -o, --output <file>      Output file: JP2, JPH, or raw J2K/J2C/JPC (required)\n"
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

/* Detect format by reading magic bytes from an existing file */
static TranscodeFormat detectInputFormat(const char* path)
{
  GRK_CODEC_FORMAT fmt;
  if(!grk_detect_format(path, &fmt))
    return TFMT_UNKNOWN;
  switch(fmt)
  {
    case GRK_CODEC_JP2:
    case GRK_CODEC_MJ2:
      return TFMT_CONTAINER;
    case GRK_CODEC_J2K:
      return TFMT_CODESTREAM;
    default:
      return TFMT_UNKNOWN;
  }
}

/* Determine output format from file extension (file doesn't exist yet) */
static TranscodeFormat formatFromExtension(const char* path)
{
  const char* dot = strrchr(path, '.');
  if(!dot)
    return TFMT_UNKNOWN;
  ++dot;
  if(strcasecmp(dot, "jp2") == 0 || strcasecmp(dot, "jph") == 0)
    return TFMT_CONTAINER;
  if(strcasecmp(dot, "j2k") == 0 || strcasecmp(dot, "j2c") == 0 || strcasecmp(dot, "jpc") == 0)
    return TFMT_CODESTREAM;
  return TFMT_UNKNOWN;
}

static uint32_t readU32BE(FILE* f)
{
  uint8_t buf[4];
  if(fread(buf, 1, 4, f) != 4)
    return 0;
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
}

static uint64_t readU64BE(FILE* f)
{
  uint8_t buf[8];
  if(fread(buf, 1, 8, f) != 8)
    return 0;
  return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) | ((uint64_t)buf[2] << 40) |
         ((uint64_t)buf[3] << 32) | ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
         ((uint64_t)buf[6] << 8) | buf[7];
}

/* Extract the raw codestream from a JP2/JPH container file.
 * Scans ISO BMFF boxes for the jp2c box and copies its contents. */
static uint64_t extractCodestream(const char* jp2Path, const char* j2kPath)
{
  FILE* in = fopen(jp2Path, "rb");
  if(!in)
  {
    fprintf(stderr, "Error: cannot open '%s' for reading\n", jp2Path);
    return 0;
  }

  fseek(in, 0, SEEK_END);
  long fileSizeSigned = ftell(in);
  fseek(in, 0, SEEK_SET);
  if(fileSizeSigned <= 0)
  {
    fprintf(stderr, "Error: cannot determine size of '%s'\n", jp2Path);
    fclose(in);
    return 0;
  }
  uint64_t fileSize = (uint64_t)fileSizeSigned;

  uint64_t csOffset = 0, csLength = 0;
  bool found = false;

  while(!found)
  {
    long pos = ftell(in);
    if(pos < 0 || (uint64_t)pos >= fileSize)
      break;

    uint32_t lbox = readU32BE(in);
    uint32_t tbox = readU32BE(in);
    if(feof(in))
      break;

    uint64_t boxLength;
    uint32_t headerSize = 8;
    if(lbox == 1)
    {
      boxLength = readU64BE(in);
      headerSize = 16;
    }
    else if(lbox == 0)
    {
      boxLength = fileSize - (uint64_t)pos;
    }
    else
    {
      boxLength = lbox;
    }

    if(tbox == 0x6a703263) /* JP2C */
    {
      csOffset = (uint64_t)pos + headerSize;
      csLength = boxLength - headerSize;
      found = true;
    }
    else
    {
      uint64_t skip = boxLength - headerSize;
      if(fseek(in, (long)skip, SEEK_CUR) != 0)
        break;
    }
  }

  if(!found)
  {
    fprintf(stderr, "Error: no codestream box (jp2c) found in '%s'\n", jp2Path);
    fclose(in);
    return 0;
  }

  fseek(in, (long)csOffset, SEEK_SET);

  FILE* out = fopen(j2kPath, "wb");
  if(!out)
  {
    fprintf(stderr, "Error: cannot open '%s' for writing\n", j2kPath);
    fclose(in);
    return 0;
  }

  const size_t bufSize = 1024 * 1024;
  auto* buf = new uint8_t[bufSize];
  uint64_t remaining = csLength;
  uint64_t totalWritten = 0;
  bool ok = true;

  while(remaining > 0)
  {
    size_t toRead = (remaining > bufSize) ? bufSize : (size_t)remaining;
    size_t bytesRead = fread(buf, 1, toRead, in);
    if(bytesRead == 0)
    {
      ok = false;
      break;
    }
    size_t bytesWritten = fwrite(buf, 1, bytesRead, out);
    if(bytesWritten != bytesRead)
    {
      ok = false;
      break;
    }
    totalWritten += bytesWritten;
    remaining -= bytesRead;
  }

  delete[] buf;
  fclose(in);
  fclose(out);

  if(!ok)
  {
    fprintf(stderr, "Error: I/O error during codestream extraction\n");
    return 0;
  }

  return totalWritten;
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

  auto inputFmt = detectInputFormat(inputFile);
  auto outputFmt = formatFromExtension(outputFile);

  if(inputFmt == TFMT_UNKNOWN)
  {
    fprintf(stderr, "Error: '%s' is not a recognized JPEG 2000 file.\n", inputFile);
    return 1;
  }
  if(outputFmt == TFMT_UNKNOWN)
  {
    fprintf(stderr, "Error: unrecognized output extension.\n"
                    "Supported: .jp2, .jph, .j2k, .j2c, .jpc\n");
    return 1;
  }

  if(inputFmt == TFMT_CODESTREAM && outputFmt == TFMT_CONTAINER)
  {
    fprintf(stderr, "Error: converting raw codestream (J2K/J2C/JPC) to container\n"
                    "format (JP2/JPH) is not supported. Use grk_compress instead.\n");
    return 1;
  }

  bool hasModifications =
      writeTlm || writePlt || writeSop || writeEph || maxLayers > 0 || maxRes > 0 ||
      progOrder != GRK_PROG_UNKNOWN;

  /* JP2/JPH -> J2K: strip boxes, output raw codestream */
  if(inputFmt == TFMT_CONTAINER && outputFmt == TFMT_CODESTREAM)
  {
    fprintf(stderr, "Warning: converting to raw codestream — all JP2/JPH box\n"
                    "metadata (XMP, ICC profile, GeoTIFF UUID, etc.) will be discarded.\n");

    if(!hasModifications)
    {
      /* No codestream modifications: extract directly from input */
      uint64_t written = extractCodestream(inputFile, outputFile);
      if(written == 0)
        return 1;

      fprintf(stdout, "Extracted codestream %s -> %s (%" PRIu64 " bytes)\n", inputFile, outputFile,
              written);
      return 0;
    }

    /* Codestream modifications requested: transcode to temp JP2, then extract */
    grk_initialize(nullptr, 0, nullptr);

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

    std::string tmpPath = std::string(outputFile) + ".tmp.jp2";

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
    safe_strcpy(dstStreamParams.file, tmpPath.c_str());

    grk_stream_params transSrcStreamParams{};
    safe_strcpy(transSrcStreamParams.file, inputFile);

    uint64_t tmpWritten =
        grk_transcode(&transSrcStreamParams, &dstStreamParams, &cparams, srcImage);
    grk_object_unref(decCodec);

    if(tmpWritten == 0)
    {
      fprintf(stderr, "Error: transcode failed\n");
      remove(tmpPath.c_str());
      return 1;
    }

    uint64_t written = extractCodestream(tmpPath.c_str(), outputFile);
    remove(tmpPath.c_str());

    if(written == 0)
      return 1;

    fprintf(stdout, "Transcoded %s -> %s (%" PRIu64 " bytes)\n", inputFile, outputFile, written);
    return 0;
  }

  /* Container-to-container or J2K-to-J2K: use grk_transcode API */
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
