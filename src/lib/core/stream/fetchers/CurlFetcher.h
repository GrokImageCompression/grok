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

#include <memory>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <set>
#include <vector>
#include <map>
#include <sstream>
#include <functional>

#include "grk_config_private.h"
#include "FetchCommon.h"
#include "EnvVarManager.h"
#include "SimpleXmlParser.h"

#ifdef GRK_ENABLE_LIBCURL
#include <curl/curl.h>
#endif

namespace grk
{

class TPFetchSeq;
class TPSeq;
using TPSEQ_VEC = std::vector<std::unique_ptr<TPSeq>>;

struct TileFetchContext;
using TileFetchCallback = std::function<void(size_t requestIndex, TileFetchContext* context)>;

class IFetcher
{
public:
  virtual ~IFetcher() = default;

  // Initialize the fetcher with a path and authentication details
  virtual void init(const std::string& path, const FetchAuth& auth) = 0;

  // Read data into a buffer
  virtual size_t read(uint8_t* buffer, size_t numBytes) = 0;

  // Seek to a specific offset
  virtual bool seek(uint64_t offset) = 0;

  // Get the total size of the resource
  virtual uint64_t size() const = 0;

  // Get the current offset
  virtual uint64_t offset() const = 0;

  // Fetch tiles asynchronously
  virtual std::future<bool> fetchTiles(const TPSEQ_VEC& allTileParts,
                                       const std::set<uint16_t>& slated, void* user_data,
                                       TileFetchCallback callback) = 0;

  virtual void onFetchTilesComplete(std::shared_ptr<TileFetchContext> context, bool success) = 0;

  // Fetch chunks asynchronously
  virtual void fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer) = 0;

  virtual void fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer,
                           std::shared_ptr<std::vector<ChunkRequest>> requests) = 0;

  // List directory contents
  virtual std::vector<std::string> listDirectory(const std::string& path) = 0;

  // Retrieve metadata
  virtual bool getMetadata(const std::string& path,
                           std::map<std::string, std::string>& metadata) = 0;

  // Fetch throttle: when set, the fetcher pauses scheduling new HTTP requests
  // until the callback returns true.  notifyThrottleRelease() wakes the
  // fetcher so it can re-check the condition.
  virtual void setFetchThrottle(std::function<bool()> throttle) = 0;
  virtual void notifyThrottleRelease() = 0;
};

struct TileFetchContext : public std::enable_shared_from_this<TileFetchContext>
{
  std::shared_ptr<TPFetchSeq> requests_;
  void* user_data_ = nullptr;
  std::shared_ptr<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>> tilePartFetchByTile_;
  TileFetchCallback callback_;
  IFetcher* fetcher_ = nullptr;

  TileFetchContext(std::shared_ptr<TPFetchSeq>& requests, void* user_data,
                   std::shared_ptr<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>>&
                       tilePartFetchByTile,
                   TileFetchCallback callback, IFetcher* fetcher)
      : requests_(requests), user_data_(user_data), tilePartFetchByTile_(tilePartFetchByTile),
        callback_(callback), fetcher_(fetcher)
  {}

  void incrementCompleteCount();

private:
  mutable size_t completeCount_ = 0;
};

typedef size_t (*CURL_FETCHER_WRITE_CALLBACK)(void* contents, size_t size, size_t nmemb,
                                              void* userp);

#ifdef GRK_ENABLE_LIBCURL

class CurlFetcher : public IFetcher
{
public:
  CurlFetcher(void);
  virtual ~CurlFetcher() override;

  void setFetchThrottle(std::function<bool()> throttle) override;
  void notifyThrottleRelease() override;
  void init(const std::string& path, const FetchAuth& auth) override;
  size_t read(uint8_t* buffer, size_t numBytes) override;
  bool seek(uint64_t offset) override;

  uint64_t size() const override
  {
    return total_size_;
  }

  uint64_t offset() const override
  {
    return current_offset_;
  }

  /**
   * @brief Initiates tile fetch by creating an @ref FetchJob and pushing this
   * onto the tile fetch queue
   *
   * @param allTileParts flat sequence of all tile parts
   * @param slated slated tile indices
   * @param user_data void* user data
   * @param callback tile fetch callback
   * @return std::future<bool> future
   */
  std::future<bool> fetchTiles(const TPSEQ_VEC& allTileParts, const std::set<uint16_t>& slated,
                               void* user_data, TileFetchCallback callback) override;

  /**
   * @brief Called when tile fetch is complete
   *
   * @param context context
   * @param success true if fetch was successful, otherwise false
   */
  void onFetchTilesComplete(std::shared_ptr<TileFetchContext> context, bool success) override;

  void fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer) override;
  void fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer,
                   std::shared_ptr<std::vector<ChunkRequest>> requests) override;

  // Directory listing
  std::vector<std::string> listDirectory(const std::string& path) override;

  // Metadata retrieval (HEAD request)
  bool getMetadata(const std::string& path,
                   std::map<std::string, std::string>& metadata) override;

protected:
  virtual curl_slist* prepareAuthHeaders(curl_slist* headers) = 0;
  virtual void parse(const std::string& path) = 0;

  void fetchError(FetchResult* result);
  virtual void auth(CURL* curl);

  curl_slist* configureHeaders(const std::string& range);

  time_t getLastModifiedTime() const
  {
    return last_modified_time_;
  }

  void fetch_total_size();

  /**
   * @brief Configures a CURL easy handle for a byte-range fetch request
   *
   * @param offset byte range start
   * @param end byte range end (inclusive)
   * @param result fetch result to receive data and status
   * @param callback write callback (tile or chunk)
   * @return configured CURL* handle
   */
  CURL* configureHandle(uint64_t offset, uint64_t end, FetchResult& result,
                        CURL_FETCHER_WRITE_CALLBACK callback);

  /**
   * @brief Schedules a tile fetch by creating TileFetchContext, generating
   * tile part collections, and scheduling the first batch of CURL requests
   *
   * @param slated set of tile indices to fetch
   * @return shared_ptr to TileFetchContext, or nullptr on failure
   */
  std::shared_ptr<TileFetchContext> scheduleTileFetch(const std::set<uint16_t>& slated);

  /**
   * @brief Schedules a chunk fetch by creating a ScheduledFetch with
   * ChunkContext and scheduling the first batch of CURL requests
   *
   * @param ctx chunk context with buffer and requests
   * @param requests chunk request descriptors
   * @param results pre-allocated result vector
   * @param promises per-request promises for async completion
   * @return true if scheduling succeeded
   */
  bool scheduleChunkFetch(std::shared_ptr<ChunkContext> ctx,
                          std::shared_ptr<std::vector<ChunkRequest>> requests,
                          std::shared_ptr<std::vector<FetchResult>> results,
                          std::shared_ptr<std::vector<std::promise<FetchResult>>> promises);

  /**
   * @brief Schedules the next batch of CURL requests from the current
   * ScheduledFetch, up to batchSize_ concurrent requests
   *
   * @param callback write callback (tile or chunk)
   * @return true if scheduling succeeded
   */
  bool scheduleNextBatch(CURL_FETCHER_WRITE_CALLBACK callback);

  /**
   * @brief Retries a failed fetch request by resetting its state
   * and re-adding a CURL handle to the multi handle
   *
   * @param result the failed fetch result to retry
   * @param offset byte range start
   * @param end byte range end
   * @param callback write callback (tile or chunk)
   * @param onFatalError called if the retry itself fails to schedule
   */
  void retryRequest(FetchResult* result, uint64_t offset, uint64_t end,
                    CURL_FETCHER_WRITE_CALLBACK callback, std::function<void()> onFatalError);

  void curl_initiate_retry(CURL* curl);

  /**
   * @brief Determines whether a failed fetch request should be retried
   *
   * @param result the fetch result to check
   * @param curl_code the CURL result code
   * @return true if the request should be retried
   */
  bool shouldRetry(const FetchResult& result, CURLcode curl_code) const;

  /**
   * @brief Recovers the byte range (offset, end) for a fetch result
   * by looking up the original request in the tile or chunk context
   *
   * @param result the fetch result
   * @return (offset, end) byte range pair
   */
  std::pair<uint64_t, uint64_t> getRequestRange(const FetchResult& result) const;

  void fetchWorker();

  static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* s)
  {
    s->append((char*)contents, size * nmemb);
    return size * nmemb;
  }

  FetchAuth auth_;
  std::string url_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::unordered_map<std::shared_ptr<TileFetchContext>, std::promise<bool>> active_jobs_;
  std::mutex active_jobs_mutex_;
  std::mutex fetch_mutex_;
  std::queue<FetchJob> tile_fetch_queue_;
  std::queue<ChunkTask> chunk_fetch_queue_;
  std::mutex active_handles_mutex_;
  std::unordered_map<CURL*, size_t> active_handles_;
  void* user_data_ = nullptr;
  uint64_t total_size_ = 0;
  uint64_t current_offset_ = 0;
  CURLM* multi_handle_ = nullptr;

private:
  size_t batchSize_ = 30;
  bool stop_ = false;
  std::thread fetchThread_;
  uint32_t maxRetries_ = 3;
  uint32_t retryDelayMs_ = 1000;
  std::function<bool()> fetchThrottle_;
  std::mutex throttleMutex_;
  std::condition_variable throttleCV_;

protected:
  TileFetchCallback tileFetchCallback_;
  const TPSEQ_VEC* allTileParts_ = nullptr;
  time_t last_modified_time_ = -1;

private:
  CURL_FETCHER_WRITE_CALLBACK tileWriteCallback_;
  ScheduledFetch currentFetch_;
};

#endif

} // namespace grk
