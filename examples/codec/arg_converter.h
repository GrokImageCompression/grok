/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstring>
#include <sstream>

class ArgConverter
{
public:
  // Constructor to initialize with the program name
  explicit ArgConverter(std::string programName);

  // Method to push a command line argument and its value
  template<typename T>
  void push(const std::string& arg, T value);

  // Method to push an option without a value
  void push(const std::string& arg);

  // Method to get the argument count (argc)
  int argc();

  // Method to get the argument vector (argv)
  const char** argv();

  // Destructor to clean up dynamically allocated memory
  ~ArgConverter();

private:
  std::unordered_map<std::string, std::string> argsMap;
  std::vector<std::string> argsVector;
  std::vector<char*> argsCStrings;
  std::string programName;

  // Helper method to convert the map to a vector
  void convertToVector();
};

// Implementation

ArgConverter::ArgConverter(std::string programName) : programName(std::move(programName))
{
  argsVector.push_back(this->programName);
}

template<typename T>
void ArgConverter::push(const std::string& arg, T value)
{
  // Use std::ostringstream to convert value to string
  std::ostringstream oss;
  oss << value;
  std::string valueStr = oss.str();

  // Check if the argument already exists, if so, update it
  argsMap[arg] = valueStr;
}

// Specialization for std::string to avoid redundant conversion
template<>
void ArgConverter::push<std::string>(const std::string& arg, std::string value)
{
  argsMap[arg] = std::move(value);
}

void ArgConverter::push(const std::string& arg)
{
  // Add the option without a value
  argsMap[arg] = "";
}

int ArgConverter::argc()
{
  convertToVector();
  return (int)argsVector.size();
}

const char** ArgConverter::argv()
{
  convertToVector();
  // Clean up previous C strings
  for(char* arg : argsCStrings)
  {
    delete[] arg;
  }
  argsCStrings.clear();

  // Convert vector to array of C strings
  for(auto& arg : argsVector)
  {
    char* cstr = new char[arg.size() + 1];
    std::strcpy(cstr, arg.c_str());
    argsCStrings.push_back(cstr);
  }

  return const_cast<const char**>(argsCStrings.data());
}

void ArgConverter::convertToVector()
{
  // Clear the vector
  argsVector.clear();
  // Add the program name
  argsVector.push_back(
      "program_name"); // You can replace this with the actual program name if needed

  // Add all arguments from the map to the vector
  for(const auto& kv : argsMap)
  {
    argsVector.push_back(kv.first);
    if(!kv.second.empty())
    {
      argsVector.push_back(kv.second);
    }
  }
}

ArgConverter::~ArgConverter()
{
  // Clean up dynamically allocated memory
  for(char* arg : argsCStrings)
  {
    delete[] arg;
  }
}
