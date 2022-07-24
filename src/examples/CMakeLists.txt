if (BUILD_TESTING)
	configure_file(
	 ${CMAKE_CURRENT_SOURCE_DIR}/grk_examples_config.h.cmake.in
	 ${CMAKE_CURRENT_BINARY_DIR}/grk_examples_config.h
	 @ONLY
	 )

	if (GRK_BUILD_LIBRARY_EXAMPLES)
		add_subdirectory(library)
	endif()
	if (GRK_BUILD_CODEC_EXAMPLES)
		add_subdirectory(codec)
	endif()
endif(BUILD_TESTING)
