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
*
 * The copyright in this software is being made available under the 3-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 1987, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grok_getopt.h"

int grok_opterr = 1,			/* if error message should be printed */
    grok_optind = 1,			/* index into parent argv vector */
    grok_optopt,			/* character checked for validity */
    grok_optreset;			/* reset getopt */
char *grok_optarg;			/* argument associated with option */

#define	BADCH	(int)'?'
#define	BADARG	(int)':'
static char EMSG[]= {""};

/*
 * getopt --
 *	Parse argc/argv argument vector.
 */
int grok_getopt(int nargc, char *const *nargv, const char *ostr)
{
#  define __progname nargv[0]
    static char *place = EMSG;	/* option letter processing */
    const char *oli = nullptr;	/* option letter list index */

    if (grok_optreset || !*place) {	/* update scanning pointer */
        grok_optreset = 0;
        if (grok_optind >= nargc || *(place = nargv[grok_optind]) != '-') {
            place = EMSG;
            return (-1);
        }
        if (place[1] && *++place == '-') {	/* found "--" */
            ++grok_optind;
            place = EMSG;
            return (-1);
        }
    }				/* option letter okay? */
    if ((grok_optopt = (int) *place++) == (int) ':' ||
            !(oli = strchr(ostr, grok_optopt))) {
        /*
         * if the user didn't specify '-' as an option,
         * assume it means -1.
         */
        if (grok_optopt == (int) '-')
            return (-1);
        if (!*place)
            ++grok_optind;
        if (grok_opterr && *ostr != ':') {
            fprintf(stderr,
                    "[ERROR] %s: illegal option -- %c\n", __progname, grok_optopt);
            return (BADCH);
        }
    }
	//strchr could return null pointer
	if (!oli)
		return (BADCH);
    if (*++oli != ':') {		/* don't need argument */
        grok_optarg = nullptr;
        if (!*place)
            ++grok_optind;
    } else {			/* need an argument */
        if (*place)			/* no white space */
            grok_optarg = place;
        else if (nargc <= ++grok_optind) {	/* no arg */
            place = EMSG;
            if (*ostr == ':')
                return (BADARG);
            if (grok_opterr) {
                fprintf(stderr,
                        "[ERROR] %s: option requires an argument -- %c\n",
                        __progname, grok_optopt);
                return (BADCH);
            }
        } else			/* white space */
            grok_optarg = nargv[grok_optind];
        place = EMSG;
        ++grok_optind;
    }
    return (grok_optopt);		/* dump back option letter */
}


int grok_getopt_long(int argc, char * const argv[], const char *optstring,
                    const grok_option_t *longopts, int totlen)
{
    static int lastidx,lastofs;
    const char *tmp;
    int i,len;
    char param = 1;

again:
    if (grok_optind >= argc || !argv[grok_optind] || *argv[grok_optind]!='-')
        return -1;

    if (argv[grok_optind][0]=='-' && argv[grok_optind][1]==0) {
        if(grok_optind >= (argc - 1)) { /* no more input parameters */
            param = 0;
        } else { /* more input parameters */
            if(argv[grok_optind + 1][0] == '-') {
                param = 0; /* Missing parameter after '-' */
            } else {
                param = 2;
            }
        }
    }

    if (param == 0) {
        ++grok_optind;
        return (BADCH);
    }

    if (argv[grok_optind][0]=='-') {	/* long option */
        char* arg=argv[grok_optind]+1;
        const grok_option_t* o;
        o=longopts;
        len=sizeof(longopts[0]);

        if (param > 1) {
            arg = argv[grok_optind+1];
            grok_optind++;
        } else
            arg = argv[grok_optind]+1;

        if(strlen(arg)>1) {
            for (i=0; i<totlen; i=i+len,o++) {
                if (!strcmp(o->name,arg)) {	/* match */
                    if (o->has_arg == 0) {
                        if ((argv[grok_optind+1])&&(!(argv[grok_optind+1][0]=='-'))) {
                            fprintf(stderr,"[ERROR] %s: option does not require an argument. Ignoring %s\n",arg,argv[grok_optind+1]);
                            ++grok_optind;
                        }
                    } else {
                        grok_optarg=argv[grok_optind+1];
                        if(grok_optarg) {
                            if (grok_optarg[0] == '-') { /* Has read next input parameter: No arg for current parameter */
                                if (grok_opterr) {
                                    fprintf(stderr,"[ERROR] %s: option requires an argument\n",arg);
                                    return (BADCH);
                                }
                            }
                        }
                        if (!grok_optarg && o->has_arg==1) {	/* no argument there */
                            if (grok_opterr) {
                                fprintf(stderr,"[ERROR] %s: option requires an argument \n",arg);
                                return (BADCH);
                            }
                        }
                        ++grok_optind;
                    }
                    ++grok_optind;
                    if (o->flag)
                        *(o->flag)=o->val;
                    else
                        return o->val;
                    return 0;
                }
            }/*(end for)String not found in the list*/
            fprintf(stderr,"[ERROR] Invalid option %s\n",arg);
            ++grok_optind;
            return (BADCH);
        } else { /*Single character input parameter*/
            if (*optstring==':') return ':';
            if (lastidx!=grok_optind) {
                lastidx=grok_optind;
                lastofs=0;
            }
            grok_optopt=argv[grok_optind][lastofs+1];
            if ((tmp=strchr(optstring,grok_optopt))) {/*Found input parameter in list*/
                if (*tmp==0) {	/* apparently, we looked for \0, i.e. end of argument */
                    ++grok_optind;
                    goto again;
                }
                if (tmp[1]==':') {	/* argument expected */
                    if (tmp[2]==':' || argv[grok_optind][lastofs+2]) {	/* "-foo", return "oo" as grok_optarg */
                        if (!*(grok_optarg=argv[grok_optind]+lastofs+2)) grok_optarg=0;
                        goto found;
                    }
                    grok_optarg=argv[grok_optind+1];
                    if(grok_optarg) {
                        if (grok_optarg[0] == '-') { /* Has read next input parameter: No arg for current parameter */
                            if (grok_opterr) {
                                fprintf(stderr,"[ERROR] %s: option requires an argument\n",arg);
                                return (BADCH);
                            }
                        }
                    }
                    if (!grok_optarg) {	/* missing argument */
                        if (grok_opterr) {
                            fprintf(stderr,"[ERROR] %s: option requires an argument\n",arg);
                            return (BADCH);
                        }
                    }
                    ++grok_optind;
                } else {/*Argument not expected*/
                    ++lastofs;
                    return grok_optopt;
                }
found:
                ++grok_optind;
                return grok_optopt;
            }	else {	/* not found */
                fprintf(stderr,"[ERROR] Invalid option %s\n",arg);
                ++grok_optind;
                return (BADCH);
            }/*end of not found*/

        }/* end of single character*/
    }/*end '-'*/
    fprintf(stderr,"[ERROR] Invalid option\n");
    ++grok_optind;
    return (BADCH);;
}/*end function*/
