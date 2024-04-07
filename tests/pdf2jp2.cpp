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
 *
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 */

/*
 * Extract all JP2 files contained within a PDF file.
 *
 * Technically you could simply use mutool, eg:
 *
 * $ mutool show -be -o obj58.jp2 Bug691816.pdf 58
 *
 * to extract a given JP2 file from within a PDF
 * However it happens sometimes that the PDF is itself corrupted, this tools is
 * a lame PDF parser which only extract stream contained in JPXDecode box
 *
 * Todo: Add support for other signatures:
 *
 * obj<</Subtype/Image/Length 110494/Filter/JPXDecode/BitsPerComponent
 * 8/ColorSpace/DeviceRGB/Width 712/Height 1052>>stream
 */
#define _GNU_SOURCE
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32

const void* memmem(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen)
{
   // Sanity check
   if(needlelen > haystacklen)
	  return nullptr;

   // Void is useless -- we must treat our data as bytes (== unsigned chars)
   typedef const unsigned char* p;

   // We'll stop searching at the last possible position for a match,
   // which is haystack[ haystacklen - needlelen + 1 ]
   haystacklen -= needlelen - 1;

   while(haystacklen)
   {
	  // Find the first byte in a potential match
	  p z = (p)memchr((p)haystack, *(p)needle, haystacklen);
	  if(!z)
		 return nullptr;

	  // Is there enough space for there to actually be a match?
	  ptrdiff_t delta = z - (p)haystack;
	  ptrdiff_t remaining = (ptrdiff_t)haystacklen - delta;
	  if(remaining < 1)
		 return nullptr;

	  // Advance our pointer and update the amount of haystack remaining
	  haystacklen -= delta;
	  haystack = z;

	  // Did we find a match?
	  if(!memcmp(haystack, needle, needlelen))
		 return haystack;

	  // Ready for next loop
	  haystack = (p)haystack + 1;
	  haystacklen -= 1;
   }
   return nullptr;
}

#endif

int main(int argc, char* argv[])
{
#define NUMJP2 32
   int i, c = 0;
   long offsets[NUMJP2];
   char buffer[512];
#define BUFLEN 4096
   int cont = 1;
   FILE* f;
   size_t nread;
   char haystack[BUFLEN];
   const char needle[] = "JPXDecode";
#define JP2_RFC3745_MAGIC "\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a"

   const size_t nlen = strlen(needle);
   const size_t flen = BUFLEN - nlen;
   char* fpos = haystack + nlen;
   const char* filename;
   if(argc < 2)
	  return 1;

   filename = argv[1];

   // zero out length of needle at beginning of haystack
   memset(haystack, 0, nlen);

   f = fopen(filename, "rb");
   while(cont)
   {
	  // read in "BUFLEN - nlen" bytes at "nlen" offset in haystack buffer
	  nread = fread(fpos, 1, flen, f);
	  size_t hlen = nlen + nread;
	  const char* ret = (const char*)memmem(haystack, hlen, needle, nlen);
	  if(ret)
	  {
		 const long cpos = ftell(f);
		 const ptrdiff_t diff = ret - haystack;
		 assert(diff >= 0);
		 /*fprintf( stdout, "Found it: %lx\n", (ptrdiff_t)cpos - (ptrdiff_t)hlen +
		  * diff);*/
		 offsets[c++] = (ptrdiff_t)cpos - (ptrdiff_t)hlen + diff;
	  }
	  cont = (nread == flen);
	  memcpy(haystack, haystack + nread, nlen);
   }

   assert(feof(f));
   for(i = 0; i < c; ++i)
   {
	  int s, len = 0;
	  char* r;
	  const int ret = fseek(f, offsets[i], SEEK_SET);
	  assert(ret == 0);
	  r = fgets(buffer, sizeof(buffer), f);
	  assert(r);
	  /*fprintf( stderr, "DEBUG: %s", r );*/
	  s = sscanf(r, "JPXDecode/Length  %u/", &len);
	  if(s == 0)
	  {
		 // try again harder
		 const int ret = fseek(f, offsets[i] - 40, SEEK_SET); // 40 is magic number
		 assert(ret == 0);
		 r = fgets(buffer, sizeof(buffer), f);
		 assert(r);
		 const char needle2[] = "/Length";
		 char* s2 = strstr(buffer, needle2);
		 if(s2)
			s = sscanf(s2, "/Length  %u/", &len);
	  }
	  if(s == 1)
	  {
		 const int ret = fseek(f, offsets[i], SEEK_SET);
		 // now look for signature box
		 fread(buffer, 1, 512, f);
		 const char* sigRet = (const char*)memmem(buffer, 512, JP2_RFC3745_MAGIC, 12);
		 if(sigRet)
		 {
			// seek to beginning of magic
			long diff = (long)((ptrdiff_t)sigRet - (ptrdiff_t)buffer);
			fseek(f, offsets[i] + diff, SEEK_SET);

			// now read len bytes into file - this is the jp2 image
			FILE* jp2;
			std::string jp2fn = filename + std::string(".") + std::to_string(i) + ".jp2";
			jp2 = fopen(jp2fn.c_str(), "wb");
			for(int j = 0; j < len; ++j)
			{
			   char v = fgetc(f);
			   int ret2 = fputc(v, jp2);
			   assert(ret2 != EOF);
			}
			fclose(jp2);
		 }
#if 0
            /* TODO need to check we reached endstream */
            r = fgets(buffer, sizeof(buffer), f);
            fprintf( stderr, "DEBUG: [%s]", r );
            r = fgets(buffer, sizeof(buffer), f);
            fprintf( stderr, "DEBUG: [%s]", r );
#endif
	  }
   }
   fclose(f);
   return 0;
}
