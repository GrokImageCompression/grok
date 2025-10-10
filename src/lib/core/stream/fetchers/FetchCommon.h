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

#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <set>
#include <future>

#include "ChunkBuffer.h"

namespace grk
{

struct ParsedFetchPath
{
  std::string host;
  std::string bucket;
  std::string key;
  int port = 443;
};

struct FetchAuth
{
  std::string username_;
  std::string password_;
  std::string bearer_token_;
  std::string custom_header_;
  std::string region_;
  std::string session_token_; // Added for AWS_SESSION_TOKEN
  FetchAuth() = default;
  FetchAuth(const std::string& u, const std::string& p, const std::string& t, const std::string& h,
            const std::string& r = "", const std::string& st = "") // Added session_token parameter
      : username_(u), password_(p), bearer_token_(t), custom_header_(h), region_(r),
        session_token_(st)
  {}
};

template<typename C>
struct TileResult
{
  TileResult() = default;
  TileResult(size_t id) : requestIndex_(id), retryCount_(0) {}

  std::shared_ptr<C> ctx_;
  size_t requestIndex_ = 0;
  std::vector<uint8_t> data_;
  long responseCode_ = 0;
  bool success_ = false;
  uint32_t retryCount_; // Added to track retries
};

// Job struct for tile fetch queue
struct FetchJob
{
  FetchJob(std::set<uint16_t>&& s) : slated(std::move(s)) {}
  std::set<uint16_t> slated;
  std::promise<bool> promise_;
};

struct DataSlice
{
  DataSlice(uint64_t offset, uint64_t length)
      : offset_(offset), length_(length), end_(length_ > 0 ? offset_ + length_ - 1 : offset)
  {}
  uint64_t offset_;
  uint64_t length_;
  uint64_t end_;
};

// Request for a chunk fetch
struct ChunkRequest : public DataSlice
{
  ChunkRequest(uint16_t id, uint64_t start, uint64_t end)
      : DataSlice(start, end >= start ? end - start + 1 : 0), requestIndex_(id)
  {}
  uint16_t requestIndex_;
};

struct ChunkContext;

struct ChunkResult
{
  ChunkResult() = default;
  ChunkResult(uint16_t id) : requestIndex_(id), retryCount_(0) {}

  std::shared_ptr<ChunkContext> ctx_;
  uint16_t requestIndex_ = 0;
  std::vector<uint8_t> data_; // Buffer containing fetched data
  long responseCode_ = 0; // HTTP response code
  bool success_ = false; // Indicates if the fetch was successful
  uint32_t retryCount_; // Added to track retries
};

struct ChunkContext
{
  ChunkContext(std::shared_ptr<ChunkBuffer<>> chunkBuffer,
               std::shared_ptr<std::vector<ChunkRequest>> requests)
      : chunkBuffer_(chunkBuffer), requests_(requests)
  {}
  std::shared_ptr<ChunkBuffer<>> chunkBuffer_;
  std::shared_ptr<std::vector<ChunkRequest>> requests_;
};

// Job struct for chunk fetch queue
struct ChunkTask
{
  ChunkTask(std::shared_ptr<ChunkBuffer<>> chunkBuffer,
            std::shared_ptr<std::vector<ChunkRequest>> reqs)
      : chunkBuffer_(chunkBuffer), requests_(reqs)
  {
    promises_.resize(requests_->size());
  }
  std::shared_ptr<ChunkBuffer<>> chunkBuffer_;
  std::shared_ptr<std::vector<ChunkRequest>> requests_;
  std::vector<std::promise<ChunkResult>> promises_;
};

// Struct to manage a scheduled batch of chunk fetches
struct ScheduledChunkFetch
{
  ScheduledChunkFetch(std::shared_ptr<ChunkContext> ctx,
                      std::shared_ptr<std::vector<ChunkRequest>> reqs,
                      std::shared_ptr<std::vector<ChunkResult>> res,
                      std::shared_ptr<std::vector<std::promise<ChunkResult>>> proms)
      : ctx_(ctx), requests_(reqs), results_(res), promises_(proms), scheduled_(0), completed_(0)
  {
    if(requests_)
      requestIter_ = requests_->begin();
  }
  ScheduledChunkFetch() = default;

  std::shared_ptr<ChunkContext> ctx_;
  std::shared_ptr<std::vector<ChunkRequest>> requests_;
  std::shared_ptr<std::vector<ChunkResult>> results_;
  std::shared_ptr<std::vector<std::promise<ChunkResult>>> promises_;
  std::vector<ChunkRequest>::iterator requestIter_;
  size_t scheduled_; // Total scheduled requests so far
  size_t completed_; // Total completed requests
};

} // namespace grk