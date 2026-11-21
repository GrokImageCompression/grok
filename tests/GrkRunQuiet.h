#pragma once

#include <cstdlib>
#include <string>

inline int runQuiet(const std::string& command)
{
#ifdef _WIN32
  // cmd /c strips the first and last quote of the line
  return std::system(("\"" + command + " > NUL\"").c_str());
#else
  return std::system((command + " > /dev/null").c_str());
#endif
}
