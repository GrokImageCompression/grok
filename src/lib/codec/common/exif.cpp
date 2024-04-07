/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
#include <iostream>

#ifdef GROK_HAVE_EXIFTOOL
#ifndef _MSC_VER
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wdeprecated-volatile"
#else
#pragma GCC diagnostic ignored "-Wvolatile"
#endif
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
#endif
#include "exif.h"
#include <stdexcept>

namespace grk
{
#ifdef GROK_HAVE_EXIFTOOL
class PerlInterp
{
  public:
	PerlInterp() : my_perl(nullptr)
	{
		constexpr int NUM_ARGS = 3;
		PERL_SYS_INIT3(nullptr, nullptr, nullptr);
		my_perl = perl_alloc();
		if(my_perl)
		{
			perl_construct(my_perl);
			const char* embedding[NUM_ARGS] = {"", "-e", "0"};
			if(perl_parse(my_perl, nullptr, NUM_ARGS, (char**)embedding, nullptr))
			{
				dealloc();
				throw std::runtime_error("Unable to parse Perl script used to extract exif tags");
			}
			if(perl_run(my_perl))
			{
				dealloc();
				throw std::runtime_error(
					"Unable to run Perl interpreter used to extract exif tags");
			}
			std::string script{R"x(
	                use strict;
					use warnings;
					use Image::ExifTool;
					sub transfer {
						my $srcFile = $_[0];
						my $outFile = $_[1];
						my $exifTool = new Image::ExifTool;
						my $info = $exifTool->SetNewValuesFromFile($srcFile, 'all:all');
						my $result = $exifTool->WriteInfo($outFile);
					}
			    )x"};
			if(!eval_pv(script.c_str(), TRUE))
			{
				dealloc();
				throw std::runtime_error(
					"Unable to evaluate Perl script used to extract exif tags");
			}
		}
		else
		{
			PERL_SYS_TERM();
		}
	}

	~PerlInterp()
	{
		dealloc();
	}

  private:
	void dealloc(void)
	{
		if(my_perl)
		{
			perl_destruct(my_perl);
			perl_free(my_perl);
			PERL_SYS_TERM();
		}
		my_perl = nullptr;
	}
	PerlInterpreter* my_perl;
};

class PerlScriptRunner
{
  public:
	static PerlInterp* instance(void)
	{
		static PerlInterp interp;
		return &interp;
	}
};
#endif

void transferExifTags([[maybe_unused]] const std::string &src, [[maybe_unused]] const std::string &dest)
{
#ifdef GROK_HAVE_EXIFTOOL
	try
	{
		PerlScriptRunner::instance();
	}
	catch(const std::runtime_error& re)
	{
		std::cout << re.what() << std::endl;
		return;
	}
	dTHX;
	char* args[] = {(char*)src.c_str(), (char*)dest.c_str(), nullptr};
	if(call_argv("transfer", G_DISCARD, args))
		std::cout << "Unable to run Perl script used to extract exif tags" << std::endl;
#else
	std::cout << "ExifTool not available; unable to transfer Exif tags" << std::endl;
#endif
}

} // namespace grk
