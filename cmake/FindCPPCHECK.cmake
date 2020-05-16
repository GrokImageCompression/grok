find_program(CPPCHECK_EXECUTABLE cppcheck )
mark_as_advanced( CPPCHECK_EXECUTABLE  )

if (NOT CPPCHECK_EXECUTABLE)
  if (CPPCHECK_FIND_REQUIRED)
    message(FATAL_ERROR "ERROR: Could not find cppcheck")
  endif ()
endif ()
