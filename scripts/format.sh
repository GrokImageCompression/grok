find . \( -type d \( -name CLI11 -o -name highway -o -name thirdparty -o -name taskflow -o -name tclap  -o -name spdlog -o -name coding -o -name others -o -name common \) -prune \) \
-o \( -iname *.h -o -iname *.c -o -iname *.cpp -o -iname *.hpp \) -print \
| xargs clang-format -style=file -i -fallback-style=none
