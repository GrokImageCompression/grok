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

#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <set>
#include <future>
#include <functional>

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
  std::string s3_endpoint_; // Custom S3 endpoint URL (e.g. MinIO)
  int8_t s3_use_https_ = 0; // 0 = auto, 1 = HTTPS, -1 = HTTP
  int8_t s3_use_virtual_hosting_ = 0; // 0 = auto, 1 = virtual-hosted, -1 = path-style
  bool s3_no_sign_request_ = false; // true = skip authentication
  bool s3_allow_insecure_ = false; // true = disable SSL certificate verification
  FetchAuth() = default;
  FetchAuth(const std::string& u, const std::string& p, const std::string& t, const std::string& h,
            const std::string& r = "", const std::string& st = "")
      : username_(u), password_(p), bearer_token_(t), custom_header_(h), region_(r),
        session_token_(st)
  {}
};

// Unified fetch result — replaces TileResult<C> and ChunkResult
struct FetchResult
{
  FetchResult() = default;
  FetchResult(size_t id) : requestIndex_(id) {}

  std::shared_ptr<void> ctx_; // type-erased context (TileFetchContext or ChunkContext)
  size_t requestIndex_ = 0;
  std::vector<uint8_t> data_;
  long responseCode_ = 0;
  bool success_ = false;
  uint32_t retryCount_ = 0;
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
  std::vector<std::promise<FetchResult>> promises_;
};

// Abstract interface for iterating over fetch requests
struct IRequestBatch
{
  virtual ~IRequestBatch() = default;
  virtual bool hasMore() const = 0;
  virtual size_t remaining() const = 0;
  // Returns (offset, end) for the next request
  virtual std::pair<uint64_t, uint64_t> next() = 0;
};

// Unified scheduled fetch — replaces ScheduledTileFetch and ScheduledChunkFetch
struct ScheduledFetch
{
  ScheduledFetch() = default;
  ScheduledFetch(std::shared_ptr<void> ctx, std::unique_ptr<IRequestBatch> requests,
                 std::shared_ptr<std::vector<FetchResult>> results,
                 std::shared_ptr<std::vector<std::promise<FetchResult>>> promises = nullptr)
      : ctx_(std::move(ctx)), requests_(std::move(requests)), results_(std::move(results)),
        promises_(std::move(promises))
  {}

  std::shared_ptr<void> ctx_;
  std::unique_ptr<IRequestBatch> requests_;
  std::shared_ptr<std::vector<FetchResult>> results_;
  std::shared_ptr<std::vector<std::promise<FetchResult>>> promises_; // null for tile path
  size_t scheduled_ = 0;
  size_t completed_ = 0;
};

// IRequestBatch adapter for chunk requests
struct ChunkRequestBatch : public IRequestBatch
{
  ChunkRequestBatch(std::shared_ptr<std::vector<ChunkRequest>> requests)
      : requests_(std::move(requests))
  {
    iter_ = requests_->begin();
  }
  bool hasMore() const override
  {
    return iter_ != requests_->end();
  }
  size_t remaining() const override
  {
    return static_cast<size_t>(requests_->end() - iter_);
  }
  std::pair<uint64_t, uint64_t> next() override
  {
    auto& req = *iter_++;
    return {req.offset_, req.end_};
  }

private:
  std::shared_ptr<std::vector<ChunkRequest>> requests_;
  std::vector<ChunkRequest>::iterator iter_;
};

} // namespace grk
