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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <EXTERN.h>
#include <perl.h>
#pragma GCC diagnostic pop
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
		sub transfer {
			my $srcFile = $_[0];
			my $outFile = $_[1];
			my $exifTool = new Image::ExifTool;
			my $info = $exifTool->SetNewValuesFromFile($srcFile, 'all:all');
			my $result = $exifTool->WriteInfo($outFile);
		}
    )x"};
    constexpr int NUM_ARGS = 3;
    const char* embedding[NUM_ARGS] = { "", "-e", "0" };
    PERL_SYS_INIT3(NULL,NULL,NULL);
    auto my_perl = perl_alloc();
    perl_construct( my_perl );
    int res = perl_parse(my_perl, NULL, NUM_ARGS, (char**)embedding, NULL);
    assert(!res);
    (void)res;
    perl_run(my_perl);
    eval_pv(script.c_str(), TRUE);
    char *args[] = {(char*)src.c_str(), (char*)dest.c_str(), NULL};
    call_argv("transfer", G_DISCARD, args);
    perl_destruct(my_perl);
    perl_free(my_perl);
    PERL_SYS_TERM();
#else
    (void)src;
    (void)dest;
    spdlog::warn("ExifTool not available; unable to transfer Exif tags");
#endif
}



}
