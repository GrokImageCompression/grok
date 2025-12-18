/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <filesystem>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif /* _WIN32 */

#include "grk_config.h"
#include "spdlogwrapper.h"
#include "common.h"
#include "grk_string.h"
#include "GrkDump.h"
#include <CLI/CLI.hpp>

namespace grk
{

typedef struct _dircnt
{
  /** Buffer for holding images read from Directory*/
  char* filename_buf;
  /** Pointer to the buffer*/
  char** filename;
} dircnt;

typedef struct _img_folder
{
  /** The directory path of the folder containing input images*/
  char* imgdirpath;
  /** Output format*/
  const char* out_format;
  /** Enable option*/
  bool set_imgdir;
  /** Enable Cod Format for output*/
  bool set_out_format;

  uint32_t flag;
} inputFolder;

static void listImages(dircnt* dirptr, char* imgdirpath);
static char nextFile(size_t imageno, dircnt* dirptr, const inputFolder* inputFolder,
                     grk_decompress_parameters* parameters);
static int parseCommandLine(int argc, const char* argv[], grk_decompress_parameters* parameters,
                            inputFolder* inputFolder);

/* -------------------------------------------------------------------------- */
static void dump_help_display(void)
{
  fprintf(stdout,
          "\nThis is the grk_dump utility from the Grok project.\n"
          "It dumps JPEG 2000 code stream info to stdout or to a given file.\n"
          "It has been compiled against Grok library v%s.\n\n",
          grk_version());

  fprintf(stdout, "Parameters:\n");
  fprintf(stdout, "-----------\n");
  fprintf(stdout, "\n");
  fprintf(stdout, "  -y, --batch-src <directory>\n");
  fprintf(stdout, "   Image file Directory path \n");
  fprintf(stdout, "  -i, --input <compressed file>\n");
  fprintf(stdout, "    REQUIRED only if an Input image directory not specified\n");
  fprintf(stdout, "    Currently accepts J2K-files and JP2-files. The file type\n");
  fprintf(stdout, "    is identified based on its suffix.\n");
  fprintf(stdout, "  -o, --output <output file>\n");
  fprintf(stdout, "    OPTIONAL\n");
  fprintf(stdout, "    Output file where file info will be dump.\n");
  fprintf(stdout, "    By default it will be in the stdout.\n");
}

static void listImages(dircnt* dirptr, char* imgdirpath)
{
  int i = 0;

  for(const auto& entry : std::filesystem::directory_iterator(imgdirpath))
  {
    strcpy(dirptr->filename[i], entry.path().filename().string().c_str());
    i++;
  }
}

static char nextFile(size_t imageno, dircnt* dirptr, const inputFolder* inputFolder,
                     grk_decompress_parameters* parameters)
{
  std::string inputFile = dirptr->filename[imageno];
  spdlog::info("File Number {} \"{}\"", imageno, inputFile);
  std::string inputFileFullPath = inputFolder->imgdirpath + inputFile;
  if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), inputFileFullPath.c_str()) != 0)
    return 1;

  std::string baseFile;
  auto pos = inputFile.rfind(".");
  if(pos == std::string::npos)
    baseFile = inputFile;
  else
    baseFile = inputFile.substr(0, pos);
  if(inputFolder->set_out_format)
  {
    std::string outfilename = inputFolder->imgdirpath + baseFile + "." + inputFolder->out_format;
    if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outfilename.c_str()) != 0)
      return 1;
  }
  return 0;
}

static int parseCommandLine(int argc, const char* argv[], grk_decompress_parameters* parameters,
                            inputFolder* inputFolder)
{
  CLI::App app{"grk_dump command line"};

  std::string inputFile;
  std::string outputFile;
  std::string imageDirectory;
  uint32_t flag = 0;

  // Pointers to options
  auto* inputFileOpt = app.add_option("-i,--input", inputFile, "input file");
  auto* outputFileOpt = app.add_option("-o,--output", outputFile, "output file");
  auto* imageDirectoryOpt = app.add_option("-y,--batch-src", imageDirectory, "image directory");
  auto* flagOpt = app.add_option("-f,--flag", flag, "flag")->default_val(0);

  app.set_help_flag("-h", "Show abreviated usage");
  app.add_flag("--help", "Show detailed usage");
  try
  {
    app.parse(argc, argv);
  }
  catch(const CLI::ParseError& e)
  {
    int ret = app.exit(e);
    if(ret == 0)
      return GrkRCUsage;
    else if(ret == 1)
      return GrkRCParseArgsFailed;
    else
      return GrkRCFail;
  }
  if(app.count("--help"))
  {
    dump_help_display();
    return GrkRCUsage;
  }

  if(inputFileOpt->count() > 0)
  {
    const char* infile = inputFile.c_str();
    // guess the format - the codec will discover the actual one
    parameters->decod_format = GRK_CODEC_JP2;
    if(grk::strcpy_s(parameters->infile, sizeof(parameters->infile), infile) != 0)
    {
      spdlog::error("Path is too long");
      return 1;
    }
  }

  if(outputFileOpt->count() > 0)
  {
    if(grk::strcpy_s(parameters->outfile, sizeof(parameters->outfile), outputFile.c_str()) != 0)
    {
      spdlog::error("Path is too long");
      return 1;
    }
  }

  if(imageDirectoryOpt->count() > 0)
  {
    inputFolder->imgdirpath = (char*)malloc(imageDirectory.length() + 1);
    if(!inputFolder->imgdirpath)
      return 1;
    strcpy(inputFolder->imgdirpath, imageDirectory.c_str());
    inputFolder->set_imgdir = true;
  }
  if(flagOpt->count() > 0)
    inputFolder->flag = flag;

  /* check for possible errors */
  if(inputFolder->set_imgdir)
  {
    if(!(parameters->infile[0] == 0))
    {
      spdlog::error("options --batch-src and -i cannot be used together.");
      return 1;
    }
    if(!inputFolder->set_out_format)
    {
      spdlog::error("When --batch-src is used, -out_fmt <FORMAT> must be used.");
      spdlog::error("Only one format allowed.\n"
                    "Valid format are PGM, PPM, PNM, PGX, BMP, TIF and RAW.");
      return 1;
    }
    if(!(parameters->outfile[0] == 0))
    {
      spdlog::error("options --batch-src and -o cannot be used together");
      return 1;
    }
  }
  else
  {
    if(parameters->infile[0] == 0)
    {
      spdlog::error("Required parameter is missing");
      spdlog::error("Example: {} -i image.j2k", argv[0]);
      spdlog::error("Help: {} -h", argv[0]);
      return 1;
    }
  }

  return 0;
}

int GrkDump::main(int argc, const char* argv[])
{
  FILE* fout = nullptr;

  grk_decompress_parameters parameters = {};
  const grk_image* image = nullptr;
  grk_object* codec = nullptr;

  size_t num_images, imageno;
  inputFolder inputFolder;
  dircnt* dirptr = nullptr;
  int rc = EXIT_SUCCESS;

  grk_initialize(nullptr, 0, nullptr);
  configureLogging("");

  /* Initialize inputFolder */
  memset(&inputFolder, 0, sizeof(inputFolder));
  inputFolder.flag = GRK_IMG_INFO | GRK_MH_INFO | GRK_MH_IND;

  /* Parse input and get user compressing parameters */
  auto ret = parseCommandLine(argc, argv, &parameters, &inputFolder);
  if(ret == GrkRCUsage)
  {
    goto cleanup;
  }
  if(ret != GrkRCSuccess)
  {
    rc = EXIT_FAILURE;
    goto cleanup;
  }
  if(parameters.decod_format == GRK_CODEC_UNK)
  {
    spdlog::error("Unknown codec format");
    rc = EXIT_FAILURE;
    goto cleanup;
  }

  /* Initialize reading of directory */
  if(inputFolder.set_imgdir)
  {
    num_images = (size_t)grk::get_num_images(inputFolder.imgdirpath);
    if(num_images == 0)
    {
      spdlog::error("Folder is empty");
      rc = EXIT_FAILURE;
      goto cleanup;
    }

    dirptr = (dircnt*)malloc(sizeof(dircnt));
    if(dirptr)
    {
      dirptr->filename_buf = (char*)malloc(num_images * GRK_PATH_LEN *
                                           sizeof(char)); /* Stores at max 10 image file names*/
      if(!dirptr->filename_buf)
      {
        rc = EXIT_FAILURE;
        goto cleanup;
      }
      dirptr->filename = (char**)malloc(num_images * sizeof(char*));
      if(!dirptr->filename)
      {
        rc = EXIT_FAILURE;
        goto cleanup;
      }
      for(size_t i = 0; i < num_images; i++)
        dirptr->filename[i] = dirptr->filename_buf + i * GRK_PATH_LEN;
      listImages(dirptr, inputFolder.imgdirpath);
    }
  }
  else
  {
    num_images = 1;
  }

  /* Try to open for writing the output file if necessary */
  if(parameters.outfile[0] != 0)
  {
    fout = fopen(parameters.outfile, "w");
    if(!fout)
    {
      spdlog::error("failed to open {} for writing", parameters.outfile);
      rc = EXIT_FAILURE;
      goto cleanup;
    }
  }
  else
    fout = stdout;

  /* Read the header of each image one by one */
  for(imageno = 0; imageno < num_images; imageno++)
  {
    if(inputFolder.set_imgdir)
    {
      if(nextFile(imageno, dirptr, &inputFolder, &parameters))
        continue;
    }
    grk_stream_params stream_params = {};
    safe_strcpy(stream_params.file, parameters.infile);
    codec = grk_decompress_init(&stream_params, &parameters);
    if(!codec)
    {
      spdlog::error("grk_dump: failed to set up the decompressor");
      rc = EXIT_FAILURE;
      goto cleanup;
    }

    /* Read the main header of the code stream and if necessary the JP2 boxes*/
    if(!grk_decompress_read_header(codec, nullptr))
    {
      spdlog::error("grk_dump: failed to read the header");
      rc = EXIT_FAILURE;
      goto cleanup;
    }

    grk_dump_codec(codec, inputFolder.flag, fout);
    if(codec)
    {
      grk_object_unref(codec);
      codec = nullptr;
    }
    if(image)
      image = nullptr;
  }
cleanup:
  if(dirptr)
  {
    free(dirptr->filename_buf);
    free(dirptr->filename);
    free(dirptr);
  }
  grk_object_unref(codec);
  if(fout)
    fclose(fout);

  return rc;
}

} // namespace grk
