/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <vector>
#include <string>
#include <optional>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <functional>

#if defined(_WIN32)
#include <windows.h>
#include <io.h> // for SetFilePointer, etc. if needed
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace grk
{

namespace fs = std::filesystem;

template<typename T>
class TLMFile
{
public:
  static bool store(const std::vector<T>& data, const std::string& path)
  {
    static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>,
                  "T must be a POD type for binary serialization.");

    auto time = getLastModified(path);
    if(!time)
      return false;
    auto filename = generateFilename(path, time.value());

    // Check if file already exists in any search path
    auto searchPaths = getSearchPaths();
    for(const auto& dir : searchPaths)
    {
      fs::path fullPath = fs::path(dir) / filename;
      if(fs::exists(fullPath))
        return true;
    }

    auto cacheDir = getCacheDir();

    // Try cache dir first
    fs::path dirPath(cacheDir);
    try
    {
      fs::create_directories(dirPath);
      fs::path fullPath = dirPath / filename;
      if(writeToFileWithLock(fullPath, data))
        return true;
    }
    catch(...)
    {
      // Fallback to temp dir
    }

    std::string tempDir = getTempDir();
    fs::path tempDirPath(tempDir);
    try
    {
      fs::create_directories(tempDirPath);
      fs::path fullPath = tempDirPath / filename;
      return writeToFileWithLock(fullPath, data);
    }
    catch(...)
    {
      return false;
    }
  }

  static std::optional<std::vector<T>> load(const std::string& path)
  {
    static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>,
                  "T must be a POD type for binary serialization.");
    auto time = getLastModified(path);
    if(!time)
      return std::nullopt;
    auto filename = generateFilename(path, time.value());

    auto searchPaths = getSearchPaths();
    for(const auto& dir : searchPaths)
    {
      fs::path fullPath = fs::path(dir) / filename;
      if(!fs::exists(fullPath))
        continue;

      auto data = readFromFileWithLock(fullPath);
      if(data)
        return data;
    }
    return std::nullopt;
  }

private:
  static bool writeToFileWithLock(const fs::path& fullPath, const std::vector<T>& data)
  {
#if defined(_WIN32)
    HANDLE hFile = CreateFileA(fullPath.string().c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE)
      return false;

    OVERLAPPED overlapped = {};
    BOOL locked = LockFileEx(hFile, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, 0, &overlapped);
    if(!locked)
    {
      CloseHandle(hFile);
      return false;
    }

    LARGE_INTEGER fileSize = {};
    GetFileSizeEx(hFile, &fileSize);
    if(fileSize.QuadPart >= sizeof(size_t))
    {
      UnlockFileEx(hFile, 0, MAXDWORD, 0, &overlapped);
      CloseHandle(hFile);
      return true;
    }

    // Write from beginning
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD bytesWritten;
    size_t vecSize = data.size();
    WriteFile(hFile, &vecSize, sizeof(vecSize), &bytesWritten, NULL);
    if(!data.empty())
    {
      WriteFile(hFile, data.data(), static_cast<DWORD>(sizeof(T) * vecSize), &bytesWritten, NULL);
    }
    SetEndOfFile(hFile);

    UnlockFileEx(hFile, 0, MAXDWORD, 0, &overlapped);
    CloseHandle(hFile);
    return true;
#else
    int fd = ::open(fullPath.string().c_str(), O_RDWR | O_CREAT, 0666);
    if(fd == -1)
      return false;

    if(::flock(fd, LOCK_EX) == -1)
    {
      ::close(fd);
      return false;
    }

    struct stat st;
    if(::fstat(fd, &st) == -1)
    {
      ::flock(fd, LOCK_UN);
      ::close(fd);
      return false;
    }

    if(st.st_size >= sizeof(size_t))
    {
      ::flock(fd, LOCK_UN);
      ::close(fd);
      return true;
    }

    ::lseek(fd, 0, SEEK_SET);
    size_t vecSize = data.size();
    if(::write(fd, &vecSize, sizeof(vecSize)) != sizeof(vecSize))
    {
      ::flock(fd, LOCK_UN);
      ::close(fd);
      return false;
    }
    if(!data.empty())
    {
      if(::write(fd, data.data(), sizeof(T) * vecSize) != static_cast<ssize_t>(sizeof(T) * vecSize))
      {
        ::flock(fd, LOCK_UN);
        ::close(fd);
        return false;
      }
    }
    ::ftruncate(fd, sizeof(vecSize) + sizeof(T) * vecSize);

    ::flock(fd, LOCK_UN);
    ::close(fd);
    return true;
#endif
  }

  static std::optional<std::vector<T>> readFromFileWithLock(const fs::path& fullPath)
  {
#if defined(_WIN32)
    HANDLE hFile =
        CreateFileA(fullPath.string().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE)
      return std::nullopt;

    OVERLAPPED overlapped = {};
    BOOL locked = LockFileEx(hFile, 0, 0, MAXDWORD, 0, &overlapped);
    if(!locked)
    {
      CloseHandle(hFile);
      return std::nullopt;
    }

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    size_t vecSize;
    DWORD bytesRead;
    if(!ReadFile(hFile, &vecSize, sizeof(vecSize), &bytesRead, NULL) ||
       bytesRead != sizeof(vecSize))
    {
      UnlockFileEx(hFile, 0, MAXDWORD, 0, &overlapped);
      CloseHandle(hFile);
      return std::nullopt;
    }

    std::vector<T> data(vecSize);
    if(vecSize > 0)
    {
      if(!ReadFile(hFile, data.data(), static_cast<DWORD>(sizeof(T) * vecSize), &bytesRead, NULL) ||
         bytesRead != static_cast<DWORD>(sizeof(T) * vecSize))
      {
        UnlockFileEx(hFile, 0, MAXDWORD, 0, &overlapped);
        CloseHandle(hFile);
        return std::nullopt;
      }
    }

    UnlockFileEx(hFile, 0, MAXDWORD, 0, &overlapped);
    CloseHandle(hFile);
    return data;
#else
    int fd = ::open(fullPath.string().c_str(), O_RDONLY);
    if(fd == -1)
      return std::nullopt;

    if(::flock(fd, LOCK_SH) == -1)
    {
      ::close(fd);
      return std::nullopt;
    }

    size_t vecSize;
    ssize_t readn = ::read(fd, &vecSize, sizeof(vecSize));
    if(readn != sizeof(vecSize))
    {
      ::flock(fd, LOCK_UN);
      ::close(fd);
      return std::nullopt;
    }

    std::vector<T> data(vecSize);
    if(vecSize > 0)
    {
      readn = ::read(fd, data.data(), sizeof(T) * vecSize);
      if(readn != static_cast<ssize_t>(sizeof(T) * vecSize))
      {
        ::flock(fd, LOCK_UN);
        ::close(fd);
        return std::nullopt;
      }
    }

    ::flock(fd, LOCK_UN);
    ::close(fd);
    return data;
#endif
  }

  static std::optional<time_t> getLastModified(const std::string& path)
  {
    try
    {
      auto ftime = fs::last_write_time(path);
      auto sctp = std::chrono::system_clock::time_point(
          std::chrono::duration_cast<std::chrono::system_clock::duration>(
              ftime.time_since_epoch()));
      return std::chrono::system_clock::to_time_t(sctp);
    }
    catch(...)
    {
      return std::nullopt;
    }
  }

  static std::string getCacheDir()
  {
#if defined(_WIN32)
    char* localAppData = getenv("LOCALAPPDATA");
    if(localAppData)
    {
      return std::string(localAppData) + "\\TLMCache";
    }
    return getTempDir();
#elif defined(__APPLE__)
    char* home = getenv("HOME");
    if(home)
    {
      return std::string(home) + "/Library/Caches/TLMCache";
    }
    return getTempDir();
#else // Linux/Unix
    char* xdgCache = getenv("XDG_CACHE_HOME");
    if(xdgCache)
    {
      return std::string(xdgCache) + "/TLMCache";
    }
    char* home = getenv("HOME");
    if(home)
    {
      return std::string(home) + "/.cache/TLMCache";
    }
    return getTempDir();
#endif
  }

  static std::string getTempDir()
  {
#if defined(_WIN32)
    char* temp = getenv("TEMP");
    if(temp)
      return temp;
    temp = getenv("TMP");
    if(temp)
      return temp;
    return "C:\\Temp";
#else
    return "/tmp";
#endif
  }

  static std::string generateFilename(const std::string& path, time_t time)
  {
    std::hash<std::string> hasher;
    size_t hashValue = hasher(path);
    return std::to_string(hashValue) + "_" + std::to_string(time);
  }

  static std::vector<std::string> getSearchPaths()
  {
    return {getCacheDir(), getTempDir()};
  }
};

} // namespace grk