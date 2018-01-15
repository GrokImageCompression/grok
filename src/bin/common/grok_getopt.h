/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
*/

#pragma once

extern "C" {

struct grok_option_t {
	const char *name;
	int has_arg;
	int *flag;
	int val;
} ;

#define	NO_ARG	0
#define REQ_ARG	1
#define OPT_ARG	2

extern int grok_opterr;
extern int grok_optind;
extern int grok_optopt;
extern int grok_optreset;
extern char *grok_optarg;

extern int grok_getopt(int nargc, char *const *nargv, const char *ostr);
extern int grok_getopt_long(int argc, char * const argv[], const char *optstring,
	const grok_option_t *longopts, int totlen);

}

