# Generate target to build the html documentation through CMake tool
# After having configured the project with the BUILD_DOC option you can run make doc
# to generate the html documentation in the doc/html repository of the build folder.

# Try to find the doxygen tool
find_package(Doxygen)

if(DOXYGEN_FOUND)
  # Configure the doxygen config file with variable from CMake and move it
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.dox.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile-html.dox @ONLY)

  # Configure the html mainpage file of the doxygen documentation with variable
  # from CMake and move it
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mainpage.dox.in
    ${CMAKE_CURRENT_BINARY_DIR}/mainpage.dox @ONLY)

  file(GLOB headers
    ${GROK_SOURCE_DIR}/src/lib/core/*.h
    ${GROK_SOURCE_DIR}/src/lib/core/*.c
  )
  # Generate new target to build the html documentation
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/html/index.html
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile-html.dox
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile-html.dox
            ${CMAKE_CURRENT_BINARY_DIR}/mainpage.dox
            ${headers}
  )
  add_custom_target(doc ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/html/index.html
    COMMENT "Building doxygen documentation"
  )

  # install HTML documentation (install png files too):
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
  )
else()
  message(STATUS "Doxygen not found, we cannot generate the documentation")
endif()
