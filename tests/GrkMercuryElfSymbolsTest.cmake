execute_process(COMMAND "${NM}" -D --defined-only "${LIBRARY}"
                OUTPUT_VARIABLE symbols COMMAND_ERROR_IS_FATAL ANY)

if(symbols MATCHES "[ \t](_R[^ \n]*|__rust_[^ \n]*|rust_eh_personality|_ZN[^ \n]*17h[0-9a-f]+E)\n")
  message(FATAL_ERROR "Grok exports a private Rust symbol: ${CMAKE_MATCH_1}")
endif()

if(NOT symbols MATCHES "[ \t]mercury_warp_loom\n")
  message(FATAL_ERROR "The Mercury C entry point is missing from Grok's exports")
endif()
