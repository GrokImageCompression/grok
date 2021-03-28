find . -type d \( -name highway -o -name spdlog -o -name coding -o -name others -o -name common \) \
-prune -false -o -iname *.h -o -iname *.c -o -iname *.cpp -o -iname *.hpp  \
| xargs clang-format -style=file -i -fallback-style=none
