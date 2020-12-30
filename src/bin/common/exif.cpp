/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
 */

#include "grk_apps_config.h"
#ifdef GROK_HAVE_EXIFTOOL
#include <EXTERN.h>
#include <perl.h>
#endif
#include "exif.h"
#include "spdlog/spdlog.h"

namespace grk {


void transferExifTags(std::string src, std::string dest){
#ifdef GROK_HAVE_EXIFTOOL
    std::string script {R"x(
						use Image::ExifTool qw(ImageInfo);
						use strict;
						use warnings;
						my $srcFile = $ARGV[0];
						my $outFile = $ARGV[1];
						my $exifTool = new Image::ExifTool;
						my $info = $exifTool->SetNewValuesFromFile($srcFile, 'all:all');
						my $result = $exifTool->WriteInfo($outFile);
    					)x"};

    PerlInterpreter *my_perl = nullptr;
    constexpr int NUM_ARGS = 5;
    const char* embedding[NUM_ARGS] = { "", "-e", "0", src.c_str(), dest.c_str() };
    my_perl = perl_alloc();
    perl_construct( my_perl );
    perl_parse(my_perl, NULL, NUM_ARGS, (char**)embedding, NULL);
    perl_run(my_perl);
    eval_pv(script.c_str(), TRUE);
    perl_destruct(my_perl);
    perl_free(my_perl);
#else
    spdlog::warn("ExifTool not available; unable to transfer Exif tags");
#endif
}

}
