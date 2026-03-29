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

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace grk
{

/**
 * @class DiskCache
 * @brief Secondary disk-based cache for compressed tile data.
 *
 * When the in-memory CompressedChunkCache evicts an entry, it is
 * spilled here. The disk cache stores each tile's raw byte blob
 * in a separate file under a temporary directory.
 *
 * Reads use standard file I/O. The directory is cleaned up on destruction.
 */
class DiskCache
{
public:
  /**
   * @brief Create a disk cache in a temporary directory.
   * @param basePath  optional base directory (default: system temp)
   */
  explicit DiskCache(const std::string& basePath = "")
  {
    namespace fs = std::filesystem;
    if(basePath.empty())
    {
      cacheDir_ = fs::temp_directory_path() / "grok_cache_XXXXXX";
      // Create a unique directory
      std::string tmpl = cacheDir_.string();
      if(!mkdtemp(tmpl.data()))
      {
        // Fallback: use PID-based name
        cacheDir_ = fs::temp_directory_path() / ("grok_cache_" + std::to_string(getpid()));
      }
      else
      {
        cacheDir_ = tmpl;
      }
    }
    else
    {
      cacheDir_ = basePath;
    }
    std::filesystem::create_directories(cacheDir_);
  }

  ~DiskCache()
  {
    std::error_code ec;
    std::filesystem::remove_all(cacheDir_, ec);
  }

  // non-copyable
  DiskCache(const DiskCache&) = delete;
  DiskCache& operator=(const DiskCache&) = delete;

  /**
   * @brief Store raw bytes to disk for a given tile.
   */
  void store(uint16_t tileIndex, const uint8_t* data, size_t size)
  {
    auto path = tilePath(tileIndex);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out)
      return;

    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    out.close();

    std::lock_guard<std::mutex> lock(mutex_);
    index_[tileIndex] = size;
  }

  /**
   * @brief Load raw bytes from disk for a given tile.
   * @return the bytes if found, or std::nullopt
   */
  std::optional<std::vector<uint8_t>> load(uint16_t tileIndex)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if(index_.find(tileIndex) == index_.end())
        return std::nullopt;
    }

    auto path = tilePath(tileIndex);
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if(!in)
      return std::nullopt;

    auto fileSize = static_cast<size_t>(in.tellg());
    if(fileSize == 0)
      return std::nullopt;

    in.seekg(0);

    std::vector<uint8_t> result(fileSize);
    in.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(fileSize));

    return result;
  }

  /**
   * @brief Check if a tile is stored on disk.
   */
  bool contains(uint16_t tileIndex) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.find(tileIndex) != index_.end();
  }

  /**
   * @brief Remove all cached entries from disk.
   */
  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& [idx, _] : index_)
    {
      std::error_code ec;
      std::filesystem::remove(tilePath(idx), ec);
    }
    index_.clear();
  }

  size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.size();
  }

private:
  std::filesystem::path tilePath(uint16_t tileIndex) const
  {
    return cacheDir_ / ("tile_" + std::to_string(tileIndex) + ".grk");
  }

  std::filesystem::path cacheDir_;
  std::unordered_map<uint16_t, size_t> index_; // tileIndex → data size
  mutable std::mutex mutex_;
};

} // namespace grk
