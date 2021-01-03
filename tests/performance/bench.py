#!/usr/bin/env python

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
