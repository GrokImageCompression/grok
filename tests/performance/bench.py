#!/usr/bin/env python

#    Copyright (C) 2016-2021 Grok Image Compression Inc.
#
#    This source code is free software: you can redistribute it and/or  modify
#    it under the terms of the GNU Affero General Public License, version 3,
#    as published by the Free Software Foundation.
#
#    This source code is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Affero General Public License for more details.
#
#    You should have received a copy of the GNU Affero General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.

import subprocess
import sys
import os

rootdir = '/home/aaron/src/grok-test-data'
extensions = ('.j2k', '.jp2')

for subdir, dirs, files in os.walk(rootdir):
    for file in files:
        ext = os.path.splitext(file)[-1].lower()
        if ext in extensions:
            print (os.path.join(subdir, file))
            try:
                result = subprocess.check_output(['/usr/bin/time', '-v',"grk_decompress", '-i', os.path.join(subdir, file), '-o', '/home/aaron/temp/' + file + '.png'],stderr=subprocess.STDOUT)
                print(result)
            except subprocess.CalledProcessError as e:
                output = str(e.output)   
                print(output)
