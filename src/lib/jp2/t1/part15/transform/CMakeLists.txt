cmake_policy(SET CMP0076 NEW)
target_sources(open_htj2k
    PRIVATE
    idwt.cpp
    fdwt.cpp
    color.cpp
    color_avx2.cpp
)