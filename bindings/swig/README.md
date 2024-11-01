## Dependencies

sudo apt install python3-dev swig

## Usage

On Linux, the bindings generate two files: `_grok_core.so` and `grok_core.py`.
These files are placed in the cmake build folder, in the bin directory.
To make use of the bindings, the PYTHONPATH must include this bin directory:

`export PYTHONPATH=~/build/grok/bin:$PYTHONPATH`
