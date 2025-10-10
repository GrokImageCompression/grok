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

#include <memory>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <set>
#include <vector>
#include <curl/curl.h>
#include <chrono> // Added for retry delay
#include <map>
#include <sstream>

namespace grk
{

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

static size_t chunkWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
  size_t total_size = size * nmemb;
  auto* res = static_cast<ChunkResult*>(userp);
  res->data_.insert(res->data_.end(), static_cast<uint8_t*>(contents),
                    static_cast<uint8_t*>(contents) + total_size);
  auto& req = (*res->ctx_->requests_)[res->requestIndex_];
  if(res->data_.size() == req.length_)
    res->ctx_->chunkBuffer_->add(res->requestIndex_, res->data_.data(), req.length_);

  return total_size;
}

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

class CurlFetcher;
struct TileFetchContext;

using TileFetchCallback = std::function<void(size_t requestIndex, TileFetchContext* context)>;

struct TileFetchContext : public std::enable_shared_from_this<TileFetchContext>
{
  std::shared_ptr<TPFetchSeq> requests_;
  void* user_data_ = nullptr;
  std::shared_ptr<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>> tilePartFetchByTile_;
  TileFetchCallback callback_; // Use the nested type
  CurlFetcher* fetcher_ = nullptr;

  TileFetchContext(std::shared_ptr<TPFetchSeq>& requests, void* user_data,
                   std::shared_ptr<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>>&
                       tilePartFetchByTile,
                   TileFetchCallback callback, // Update parameter type
                   CurlFetcher* fetcher)
      : requests_(requests), user_data_(user_data), tilePartFetchByTile_(tilePartFetchByTile),
        callback_(callback), fetcher_(fetcher)
  {}

  void incrementCompleteCount();

private:
  mutable size_t completeCount_ = 0;
};

static size_t tileWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
  size_t total_size = size * nmemb;
  auto result = static_cast<TileResult<TileFetchContext>*>(userp);
  if(result->ctx_)
  {
    auto& tpseq = (*result->ctx_->requests_)[result->requestIndex_];
    tpseq->copy(static_cast<uint8_t*>(contents), total_size);
    if(tpseq->fetchOffset_ == tpseq->length_)
    {
      result->ctx_->callback_(result->requestIndex_, result->ctx_.get());
      result->ctx_->incrementCompleteCount();
    }
  }
  else
  {
    result->data_.insert(result->data_.end(), static_cast<const uint8_t*>(contents),
                         static_cast<const uint8_t*>(contents) + total_size);
  }
  return total_size;
}

// ScheduledTileFetch struct with completed request tracking
struct ScheduledTileFetch
{
  ScheduledTileFetch(std::shared_ptr<TileFetchContext> ctx, std::shared_ptr<TPFetchSeq> requests,
                     std::shared_ptr<std::vector<TileResult<TileFetchContext>>> results)
      : ctx_(ctx), requests_(requests), results_(results)
  {
    if(requests)
      requestIter_ = requests->begin();
  }
  ScheduledTileFetch() : ScheduledTileFetch(nullptr, nullptr, nullptr) {}
  std::shared_ptr<TileFetchContext> ctx_;
  std::shared_ptr<TPFetchSeq> requests_;
  std::shared_ptr<std::vector<TileResult<TileFetchContext>>> results_;
  TPFetchSeq::iterator requestIter_;
  size_t scheduled_ = 0;
  size_t completed_ = 0;
};

typedef size_t (*CURL_FETCHER_WRITE_CALLBACK)(void* contents, size_t size, size_t nmemb,
                                              void* userp);

class CurlFetcher
{
public:
  CurlFetcher(void) : tileWriteCallback_(tileWriteCallback)
  {
    curl_global_init(CURL_GLOBAL_ALL);
    multi_handle_ = curl_multi_init();
    if(!multi_handle_)
      throw std::runtime_error("Failed to initialize CURL multi handle");
    curl_multi_setopt(multi_handle_, CURLMOPT_MAX_TOTAL_CONNECTIONS, 100L);
    fetchThread_ = std::thread(&CurlFetcher::fetchWorker, this);
  }

  virtual ~CurlFetcher()
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      stop_ = true;
    }
    queue_cv_.notify_all(); // Wake worker from wait on queue_cv_
    if(fetchThread_.joinable())
      fetchThread_.join();
    if(multi_handle_)
      curl_multi_cleanup(multi_handle_);
    curl_global_cleanup();
  }

  void init(const std::string& path, const FetchAuth& auth)
  {
    auth_ = auth;
    parse(path);
    fetch_total_size();
  }

  size_t read(uint8_t* buffer, size_t numBytes)
  {
    if(current_offset_ + numBytes > total_size_)
    {
      grklog.error("Read %zu bytes at offset %llu exceeds total size %llu", numBytes,
                   current_offset_, total_size_);
      return 0;
    }

    TileResult<TileFetchContext> result;
    auto curl = configure_handle(current_offset_, current_offset_ + numBytes - 1, result);
    auto res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
      grklog.error("curl_easy_perform failed: %s", curl_easy_strerror(res));
      curl_easy_cleanup(curl);
      return 0;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.responseCode_);
    if(result.responseCode_ != 206)
    {
      grklog.error("Read failed with HTTP code: %ld", result.responseCode_);
      curl_easy_cleanup(curl);
      return 0;
    }

    size_t bytes_read = result.data_.size();
    if(bytes_read > numBytes)
    {
      grklog.error("Received %zu bytes, but buffer only fits %zu", bytes_read, numBytes);
      bytes_read = numBytes;
    }

    std::memcpy(buffer, result.data_.data(), bytes_read);
    grklog.debug("Read %zu bytes from %llu, new offset: %llu", bytes_read, current_offset_,
                 current_offset_ + bytes_read);
    current_offset_ += bytes_read;

    curl_easy_cleanup(curl);
    return bytes_read;
  }

  bool seek(uint64_t offset)
  {
    if(offset >= total_size_)
    {
      grklog.error("Seek offset %llu exceeds total size %llu", offset, total_size_);
      return false;
    }
    current_offset_ = offset;
    grklog.debug("Seeked to offset: %llu", current_offset_);
    return true;
  }

  uint64_t size() const
  {
    return total_size_;
  }

  uint64_t offset() const
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
  std::future<bool> fetchTiles(const TPSEQ_VEC& allTileParts, std::set<uint16_t>& slated,
                               void* user_data, TileFetchCallback callback)
  {
    // 1. cache fetch data
    {
      std::lock_guard<std::mutex> lock(fetch_mutex_);
      if(!allTileParts_)
        allTileParts_ = &allTileParts;
      if(!user_data_)
        user_data_ = user_data;
      if(!tileFetchCallback_)
        tileFetchCallback_ = callback;
    }

    // 2. queue fetch
    FetchJob job(std::move(slated));
    std::future<bool> future = job.promise_.get_future();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      tile_fetch_queue_.push(std::move(job));
    }
    queue_cv_.notify_one();
    grklog.debug("Queued tile fetch job, queue size: %zu", tile_fetch_queue_.size());

    return future;
  }

  /**
   * @brief Called when tile fetch is complete
   *
   * @param context context
   * @param success true if fetch was successful, otherwise false
   */
  void onFetchTilesComplete(std::shared_ptr<TileFetchContext> context, bool success)
  {
    std::lock_guard<std::mutex> lock(active_jobs_mutex_);
    auto it = active_jobs_.find(context);
    if(it != active_jobs_.end())
    {
      it->second.set_value(success);
      active_jobs_.erase(it);
      grklog.debug("Fetch job completed");
    }
    else
    {
      grklog.error("TileFetchContext not found in active_jobs_ during completion");
    }
  }

  std::vector<std::future<ChunkResult>> fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer)
  {
    auto requests = std::make_shared<std::vector<ChunkRequest>>();
    auto length = chunkBuffer->size();
    auto offset = chunkBuffer->offset();
    auto workingLength = length - offset;
    auto chunkSize = chunkBuffer->chunkSize();
    auto numChunks = (workingLength + chunkSize - 1) / chunkSize;

    for(uint16_t i = 0; i < numChunks; ++i)
    {
      auto end = offset + chunkSize - 1;
      if(end > length - 1)
        end = length - 1;
      requests->push_back(ChunkRequest(i, offset, end));
      offset += chunkSize;
    }
    return fetchChunks(chunkBuffer, requests);
  }

  std::vector<std::future<ChunkResult>>
      fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer,
                  std::shared_ptr<std::vector<ChunkRequest>> requests)
  {
    ChunkTask task(chunkBuffer, requests);
    std::vector<std::future<ChunkResult>> futures;
    futures.reserve(task.requests_->size());
    size_t i = 0;
    for(auto& req : *task.requests_)
    {
      if(req.end_ < req.offset_ || req.end_ >= total_size_)
      {
        grklog.error("Invalid range %llu-%llu for ID %u (total size: %llu)", req.offset_, req.end_,
                     req.requestIndex_, total_size_);
        ChunkResult res(req.requestIndex_);
        res.success_ = false;
        task.promises_[i].set_value(res);
      }
      futures.push_back((task.promises_)[i].get_future());
      i++;
    }

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      chunk_fetch_queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
    grklog.debug("Queued chunk fetch task with %zu requests", requests->size());
    return futures;
  }

  // Directory listing
  std::vector<std::string> listDirectory(const std::string& path)
  {
    std::vector<std::string> files;
    CURL* curl = curl_easy_init();
    if(!curl)
    {
      grklog.error("Failed to initialize curl for directory listing");
      return files;
    }

    parse(path);
    std::string list_url = url_ + (url_.back() == '/' ? "" : "/") + "?list-type=2";
    struct curl_slist* headers = nullptr;
    headers = prepareAuthHeaders(headers);

    std::string response;

    // Temporary result for retry logic
    struct TempResult
    {
      long responseCode_ = 0;
      uint32_t retryCount_ = 0;
    } temp_result;

    do
    {
      temp_result.responseCode_ = 0;
      response.clear();
      curl_easy_setopt(curl, CURLOPT_URL, list_url.c_str());
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      auth(curl);
      curl_initiate_retry(curl); // Configure retry settings
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

      CURLcode res = curl_easy_perform(curl);
      if(res == CURLE_OK)
      {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &temp_result.responseCode_);
      }

      if(temp_result.retryCount_ < maxRetries_ &&
         (res != CURLE_OK || temp_result.responseCode_ != 200))
      {
        temp_result.retryCount_++;
        grklog.warn("Retrying directory listing for %s (retry %u/%u), HTTP %ld, CURL %d",
                    path.c_str(), temp_result.retryCount_, maxRetries_, temp_result.responseCode_,
                    res);
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs_));
      }
      else
      {
        break; // Success or max retries reached
      }
    } while(true);

    if(temp_result.responseCode_ == 200)
    {
      SimpleXmlParser parser;
      if(parser.parse(response))
      {
        files = parser.keys;
        grklog.debug("Listed %zu objects in %s", files.size(), path.c_str());
      }
      else
      {
        grklog.warn("Failed to parse ListObjectsV2 response for %s", path.c_str());
      }
    }
    else
    {
      grklog.error("Directory listing failed for %s: HTTP %ld, CURL %d after %u retries",
                   path.c_str(), temp_result.responseCode_, curl_easy_perform(curl),
                   temp_result.retryCount_);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return files;
  }

  // Metadata retrieval (HEAD request)
  bool getMetadata(const std::string& path, std::map<std::string, std::string>& metadata)
  {
    CURL* curl = curl_easy_init();
    if(!curl)
    {
      grklog.error("Failed to initialize curl for metadata retrieval");
      return false;
    }

    parse(path);
    struct curl_slist* headers = nullptr;
    headers = prepareAuthHeaders(headers);

    std::string header_data;
    bool success = false;

    // Temporary result for retry logic
    struct TempResult
    {
      long responseCode_ = 0;
      uint32_t retryCount_ = 0;
    } temp_result;

    do
    {
      temp_result.responseCode_ = 0;
      header_data.clear();
      curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
      curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      auth(curl);
      curl_initiate_retry(curl); // Configure retry settings
      curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeCallback);
      curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);

      CURLcode res = curl_easy_perform(curl);
      if(res == CURLE_OK)
      {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &temp_result.responseCode_);
      }

      if(temp_result.retryCount_ < maxRetries_ &&
         (res != CURLE_OK || temp_result.responseCode_ != 200))
      {
        temp_result.retryCount_++;
        grklog.warn("Retrying metadata retrieval for %s (retry %u/%u), HTTP %ld, CURL %d",
                    path.c_str(), temp_result.retryCount_, maxRetries_, temp_result.responseCode_,
                    res);
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs_));
      }
      else
      {
        success = (res == CURLE_OK && temp_result.responseCode_ == 200);
        break; // Success or max retries reached
      }
    } while(true);

    if(success)
    {
      // Parse headers (simplified, split by \r\n)
      std::istringstream header_stream(header_data);
      std::string line;
      while(std::getline(header_stream, line))
      {
        size_t colon_pos = line.find(':');
        if(colon_pos != std::string::npos)
        {
          std::string key = line.substr(0, colon_pos);
          std::string value = line.substr(colon_pos + 1);
          key.erase(key.find_last_not_of(" \t") + 1);
          value.erase(0, value.find_first_not_of(" \t"));
          metadata[key] = value;
          grklog.debug("Metadata: %s=%s", key.c_str(), value.c_str());
        }
      }
    }
    else
    {
      grklog.error("Metadata retrieval failed for %s: HTTP %ld, CURL %d after %u retries",
                   path.c_str(), temp_result.responseCode_, curl_easy_perform(curl),
                   temp_result.retryCount_);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
  }

protected:
  virtual curl_slist* prepareAuthHeaders(curl_slist* headers) = 0;
  virtual void parse(const std::string& path) = 0;

  void fetchError(TileResult<TileFetchContext>* result)
  {
    if(result->ctx_ && result->ctx_->fetcher_)
      result->ctx_->fetcher_->onFetchTilesComplete(result->ctx_, false);
  }
  virtual void auth(CURL* curl)
  {
    if(EnvVarManager::test_bool("GDAL_HTTP_UNSAFESSL"))
    {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    curl_easy_setopt(curl, CURLOPT_USERNAME, auth_.username_.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, auth_.password_.c_str());
  }

  curl_slist* configureHeaders(const std::string& range)
  {
    struct curl_slist* headers = nullptr;
    headers = prepareAuthHeaders(headers);
    if(!range.empty())
    {
      headers = curl_slist_append(headers, range.c_str());
    }
    return headers;
  }

  time_t getLastModifiedTime() const
  {
    return last_modified_time_;
  }

  void fetch_total_size()
  {
    CURL* curl = curl_easy_init();
    if(!curl)
      throw std::runtime_error("Failed to initialize CURL easy handle for HEAD");

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FILETIME, 1L); // Request the Last-Modified time
    auth(curl);

    auto headers = configureHeaders("");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
      grklog.error("HEAD request failed: %s", curl_easy_strerror(res));
      curl_easy_cleanup(curl);
      throw std::runtime_error("Failed to fetch file size");
    }

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if(response_code != 200)
    {
      grklog.error("HEAD request returned HTTP %ld", response_code);
      curl_easy_cleanup(curl);
      throw std::runtime_error("Invalid HEAD response");
    }

    curl_off_t content_length;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
    total_size_ = static_cast<uint64_t>(content_length);
    grklog.debug("Fetched total size: %llu bytes", total_size_);

    // Retrieve the last modified time
    long filetime;
    res = curl_easy_getinfo(curl, CURLINFO_FILETIME, &filetime);
    if(res == CURLE_OK && filetime != -1)
    {
      last_modified_time_ = static_cast<time_t>(filetime);
      grklog.debug("Fetched last modified time: %ld (Unix timestamp)", last_modified_time_);
    }
    else
    {
      grklog.warn("Last modified time not available from server");
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
  }

  virtual CURL* configure_handle(uint64_t offset, uint64_t end,
                                 TileResult<TileFetchContext>& result)
  {
    return configure<TileResult<TileFetchContext>>(offset, end, result);
  }

  template<typename R>
  CURL* configure(uint64_t offset, uint64_t end, R& result)
  {
    CURL* curl = curl_easy_init();
    if(!curl)
      throw std::runtime_error("Failed to initialize CURL easy handle");

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_initiate_retry(curl); // Modified to support retry
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    if constexpr(std::is_same_v<R, ChunkResult>)
    {
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chunkWriteCallback);
    }
    else
    {
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tileWriteCallback_);
    }
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_PRIVATE, &result);

    auth(curl);

    std::string range = "Range: bytes=" + std::to_string(offset) + "-" + std::to_string(end);
    auto headers = configureHeaders(range);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    return curl;
  }

  std::shared_ptr<TileFetchContext> scheduleTileFetch(std::set<uint16_t>& slated)
  {
    auto requests = std::make_shared<TPFetchSeq>();
    auto tilePartFetchByTile =
        std::make_shared<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>>();
    TPFetchSeq::genCollections(allTileParts_, slated, requests, tilePartFetchByTile);

    auto results = std::make_shared<std::vector<TileResult<TileFetchContext>>>(requests->size());

    auto ctx = std::make_shared<TileFetchContext>(requests, user_data_, tilePartFetchByTile,
                                                  tileFetchCallback_, this);

    return scheduleTileFetch(ScheduledTileFetch(ctx, requests, results)) ? ctx : nullptr;
  }

  bool scheduleTileFetch(ScheduledTileFetch scheduled)
  {
    currentTileFetch_ = scheduled;
    return scheduleNextTileBatch();
  }

  bool scheduleNextTileBatch(void)
  {
    if(currentTileFetch_.requestIter_ == currentTileFetch_.requests_->end())
      return true; // No more requests to schedule

    size_t activeRequests =
        currentTileFetch_.scheduled_ - currentTileFetch_.completed_; // Currently active requests
    size_t remainingBatch = batchSize_ > activeRequests ? batchSize_ - activeRequests : 0;
    size_t remainingRequests =
        static_cast<size_t>(currentTileFetch_.requests_->end() - currentTileFetch_.requestIter_);
    size_t requestsToSchedule = std::min(remainingBatch, remainingRequests);

    for(size_t i = 0; i < requestsToSchedule &&
                      currentTileFetch_.requestIter_ != currentTileFetch_.requests_->end();
        ++i)
    {
      auto req = *currentTileFetch_.requestIter_;
      uint64_t offset_ = req->offset_;
      uint64_t end_ = offset_ + req->length_ - 1;
      if(end_ >= this->total_size_)
      {
        grklog.warn("Range %llu-%llu exceeds total size %llu", offset_, end_, total_size_);
        end_ = this->total_size_ - 1;
      }
      auto& res = (*(currentTileFetch_.results_))[currentTileFetch_.scheduled_];
      res.requestIndex_ = currentTileFetch_.scheduled_;
      res.ctx_ = currentTileFetch_.ctx_;
      CURL* handle = configure_handle(offset_, end_, res);
      CURLMcode ret = curl_multi_add_handle(multi_handle_, handle);
      if(ret != CURLM_OK)
      {
        grklog.error("curl_multi_add_handle failed: %s", curl_multi_strerror(ret));
        curl_easy_cleanup(handle);
        return false;
      }
      grklog.debug("Added tile range request: %llu-%llu (index %zu)", offset_, end_,
                   currentTileFetch_.scheduled_);
      currentTileFetch_.scheduled_++;
      currentTileFetch_.requestIter_++;
    }
    return true;
  }

  virtual CURL* configureChunkHandle(uint64_t offset, uint64_t end, ChunkResult& result)
  {
    return configure<ChunkResult>(offset, end, result);
  }

  bool scheduleChunkFetch(ScheduledChunkFetch chunkFetch)
  {
    currentChunkFetch_ = chunkFetch;
    return scheduleNextChunkBatch();
  }

  bool scheduleNextChunkBatch()
  {
    if(currentChunkFetch_.requestIter_ == currentChunkFetch_.requests_->end())
      return true;

    size_t active_requests = currentChunkFetch_.scheduled_ - currentChunkFetch_.completed_;
    size_t remaining_batch = batchSize_ > active_requests ? batchSize_ - active_requests : 0;
    size_t remaining_requests =
        static_cast<size_t>(currentChunkFetch_.requests_->end() - currentChunkFetch_.requestIter_);
    size_t requests_to_schedule = std::min(remaining_batch, remaining_requests);

    for(size_t i = 0; i < requests_to_schedule &&
                      currentChunkFetch_.requestIter_ != currentChunkFetch_.requests_->end();
        ++i)
    {
      auto req = *currentChunkFetch_.requestIter_;
      uint64_t offset = req.offset_;
      uint64_t end = req.end_;
      if(end >= total_size_)
      {
        grklog.warn("Range %llu-%llu exceeds total size %llu for ID %u", offset, end, total_size_,
                    req.requestIndex_);
        end = total_size_ - 1;
      }
      auto& res = (*currentChunkFetch_.results_)[currentChunkFetch_.scheduled_];
      res.requestIndex_ = req.requestIndex_;
      res.ctx_ = currentChunkFetch_.ctx_;
      CURL* handle = configureChunkHandle(offset, end, res);
      CURLMcode ret = curl_multi_add_handle(multi_handle_, handle);
      if(ret != CURLM_OK)
      {
        grklog.error("curl_multi_add_handle failed: %s", curl_multi_strerror(ret));
        curl_easy_cleanup(handle);
        // Do not set promise here; let fetchWorker handle failures
      }
      else
      {
        active_handles_[handle] = currentChunkFetch_.scheduled_;
        grklog.debug("Scheduled chunk request %zu: ID %u, range %llu-%llu",
                     currentChunkFetch_.scheduled_, req.requestIndex_, offset, end);
        currentChunkFetch_.scheduled_++;
      }
      currentChunkFetch_.requestIter_++;
    }
    return true;
  }

  // New method to configure retry settings for CURL handle
  void curl_initiate_retry(CURL* curl)
  {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  }

  // New method to check if a request should be retried
  template<typename R>
  bool shouldRetry(const R& result, CURLcode curl_code) const
  {
    if(result.retryCount_ >= maxRetries_)
      return false;

    // Retry on CURL errors or non-206 HTTP response codes
    bool isCurlError = curl_code != CURLE_OK;
    bool isHttpError = result.responseCode_ != 206 && result.responseCode_ != 0;

    return isCurlError || isHttpError;
  }

  // Corrected method to reschedule a failed tile request
  void retryTileRequest(TileResult<TileFetchContext>* result, const std::shared_ptr<TPFetch>& req)
  {
    result->retryCount_++;
    grklog.warn("Retrying tile request %zu (retry %u/%u)", result->requestIndex_,
                result->retryCount_, maxRetries_);

    // Reset result data for retry
    result->data_.clear();
    result->responseCode_ = 0;
    result->success_ = false;

    uint64_t offset_ = req->offset_;
    uint64_t end_ = offset_ + req->length_ - 1;
    if(end_ >= total_size_)
      end_ = total_size_ - 1;

    CURL* handle = configure_handle(offset_, end_, *result);
    CURLMcode ret = curl_multi_add_handle(multi_handle_, handle);
    if(ret != CURLM_OK)
    {
      grklog.error("Retry curl_multi_add_handle failed: %s", curl_multi_strerror(ret));
      curl_easy_cleanup(handle);
      fetchError(result);
    }
    else
    {
      grklog.debug("Rescheduled tile retry %u: %llu-%llu (index %zu)", result->retryCount_, offset_,
                   end_, result->requestIndex_);
    }
  }

  // New method to reschedule a failed chunk request
  void retryChunkRequest(ChunkResult* result, const ChunkRequest& req, size_t idx)
  {
    result->retryCount_++;
    grklog.warn("Retrying chunk request ID %u (retry %u/%u)", result->requestIndex_,
                result->retryCount_, maxRetries_);

    // Reset result data for retry
    result->data_.clear();
    result->responseCode_ = 0;
    result->success_ = false;

    uint64_t offset = req.offset_;
    uint64_t end = req.end_;
    if(end >= total_size_)
      end = total_size_ - 1;

    CURL* handle = configureChunkHandle(offset, end, *result);
    CURLMcode ret = curl_multi_add_handle(multi_handle_, handle);
    if(ret != CURLM_OK)
    {
      grklog.error("Retry curl_multi_add_handle failed: %s", curl_multi_strerror(ret));
      curl_easy_cleanup(handle);
      result->success_ = false;
      (*currentChunkFetch_.promises_)[idx].set_value(*result);
    }
    else
    {
      active_handles_[handle] = idx;
      grklog.debug("Rescheduled chunk retry %u: ID %u, range %llu-%llu", result->retryCount_,
                   req.requestIndex_, offset, end);
    }
  }

  void fetchWorker()
  {
    while(!stop_)
    {
      std::vector<FetchJob> tile_jobs_to_process;
      std::vector<ChunkTask> chunk_tasks_to_process;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        while(!tile_fetch_queue_.empty())
        {
          tile_jobs_to_process.emplace_back(std::move(tile_fetch_queue_.front()));
          tile_fetch_queue_.pop();
          grklog.debug("Dequeued tile fetch job, queue size now: %zu", tile_fetch_queue_.size());
        }
        while(!chunk_fetch_queue_.empty())
        {
          chunk_tasks_to_process.emplace_back(std::move(chunk_fetch_queue_.front()));
          chunk_fetch_queue_.pop();
          grklog.debug("Dequeued chunk fetch task, queue size now: %zu", chunk_fetch_queue_.size());
        }
      }

      if(!tile_jobs_to_process.empty())
      {
        std::lock_guard<std::mutex> lock(active_jobs_mutex_);
        for(auto& job : tile_jobs_to_process)
        {
          auto ctx = scheduleTileFetch(job.slated);
          if(!ctx)
            job.promise_.set_value(false);
          else
            active_jobs_.emplace(ctx, std::move(job.promise_));
        }
      }

      if(!chunk_tasks_to_process.empty())
      {
        for(auto& task : chunk_tasks_to_process)
        {
          auto requests = task.requests_;
          auto results = std::make_shared<std::vector<ChunkResult>>(requests->size());
          auto promises =
              std::make_shared<std::vector<std::promise<ChunkResult>>>(std::move(task.promises_));
          auto ctx = std::make_shared<ChunkContext>(task.chunkBuffer_, requests);
          for(size_t i = 0; i < results->size(); ++i)
          {
            (*results)[i] = ChunkResult((*requests)[i].requestIndex_);
            (*results)[i].ctx_ = ctx;
          }
          if(!scheduleChunkFetch(ScheduledChunkFetch(ctx, requests, results, promises)))
          {
            for(size_t i = 0; i < promises->size(); ++i)
            {
              (*promises)[i].set_value((*results)[i]); // Fail all promises
            }
          }
        }
      }

      int still_running = 0;
      auto ret = curl_multi_perform(multi_handle_, &still_running);
      if(ret != CURLM_OK)
      {
        grklog.error("curl_multi_perform failed: %s", curl_multi_strerror(ret));
        std::lock_guard<std::mutex> lock(active_jobs_mutex_);
        for(auto& job : active_jobs_)
          job.second.set_value(false);
        active_jobs_.clear();
        std::lock_guard<std::mutex> lock2(active_handles_mutex_);
        for(auto& [handle, idx] : active_handles_)
        {
          ChunkResult res((*currentChunkFetch_.results_)[idx].requestIndex_);
          res.success_ = false;
          (*currentChunkFetch_.promises_)[idx].set_value(res);
          curl_multi_remove_handle(multi_handle_, handle);
          curl_easy_cleanup(handle);
        }
        active_handles_.clear();
        continue;
      }

      CURLMsg* msg;
      int msgs_left;
      while((msg = curl_multi_info_read(multi_handle_, &msgs_left)))
      {
        if(msg->msg == CURLMSG_DONE)
        {
          CURL* curl = msg->easy_handle;
          void* userp;
          curl_easy_getinfo(curl, CURLINFO_PRIVATE, &userp);

          // Determine if this is a tile or chunk fetch result
          TileResult<TileFetchContext>* tile_result = nullptr;
          ChunkResult* chunk_result = nullptr;
          if(active_handles_.find(curl) == active_handles_.end())
          {
            tile_result = static_cast<TileResult<TileFetchContext>*>(userp);
          }
          else
          {
            chunk_result = static_cast<ChunkResult*>(userp);
          }

          curl_multi_remove_handle(multi_handle_, curl);
          curl_easy_cleanup(curl);

          if(tile_result)
          {
            if(msg->data.result != CURLE_OK)
            {
              grklog.error("Tile CURL request failed: %s", curl_easy_strerror(msg->data.result));
              this->fetchError(tile_result);
            }
            else
            {
              currentTileFetch_.completed_++; // Increment completed count
              grklog.debug("Tile request %zu completed, total completed: %zu",
                           tile_result->requestIndex_, currentTileFetch_.completed_);
            }

            if(currentTileFetch_.scheduled_ > currentTileFetch_.completed_ &&
               currentTileFetch_.completed_ >= batchSize_ / 2 &&
               currentTileFetch_.requestIter_ != currentTileFetch_.requests_->end())
            {
              grklog.debug("Half of tile batch (%zu) completed, scheduling next batch",
                           batchSize_ / 2);
              scheduleNextTileBatch();
            }
          }
          else if(chunk_result)
          {
            std::lock_guard<std::mutex> lock(active_handles_mutex_);
            auto it = active_handles_.find(curl);
            if(it != active_handles_.end())
            {
              size_t idx = it->second;
              curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &chunk_result->responseCode_);
              chunk_result->success_ =
                  (msg->data.result == CURLE_OK && chunk_result->responseCode_ == 206);
              if(!chunk_result->success_)
              {
                grklog.error("Chunk fetch ID %u failed: %s, HTTP %ld", chunk_result->requestIndex_,
                             curl_easy_strerror(msg->data.result), chunk_result->responseCode_);
              }
              else
              {
                grklog.debug("Chunk fetch ID %u completed, %zu bytes", chunk_result->requestIndex_,
                             chunk_result->data_.size());
                if(tileFetchCallback_)
                  tileFetchCallback_(idx, nullptr); // No TC context for chunk fetches
              }
              (*currentChunkFetch_.promises_)[idx].set_value(*chunk_result);
              currentChunkFetch_.completed_++;
              active_handles_.erase(it);

              if(currentChunkFetch_.scheduled_ > currentChunkFetch_.completed_ &&
                 currentChunkFetch_.completed_ >= batchSize_ / 2 &&
                 currentChunkFetch_.requestIter_ != currentChunkFetch_.requests_->end())
              {
                grklog.debug("Half of chunk batch (%zu) completed, scheduling next batch",
                             batchSize_ / 2);
                scheduleNextChunkBatch();
              }
            }
          }
        }
      }

      if(still_running > 0)
      {
        grklog.trace("Still running: %d requests", still_running);
      }
      else
      {
        std::lock_guard<std::mutex> lock(active_jobs_mutex_);
        std::lock_guard<std::mutex> lock2(active_handles_mutex_);
        if(active_jobs_.empty() && active_handles_.empty())
        {
          grklog.debug("No active requests, waiting");
          std::unique_lock<std::mutex> qlock(queue_mutex_);
          queue_cv_.wait(qlock, [this] {
            return stop_ || !tile_fetch_queue_.empty() || !chunk_fetch_queue_.empty();
          });
        }
      }
    }

    std::lock_guard<std::mutex> lock(active_jobs_mutex_);
    for(auto& job : active_jobs_)
      job.second.set_value(false);
    active_jobs_.clear();

    std::lock_guard<std::mutex> lock2(active_handles_mutex_);
    for(auto& [handle, idx] : active_handles_)
    {
      ChunkResult resp((*currentChunkFetch_.results_)[idx].requestIndex_);
      resp.success_ = false;
      (*currentChunkFetch_.promises_)[idx].set_value(resp);
      curl_multi_remove_handle(multi_handle_, handle);
      curl_easy_cleanup(handle);
    }
    active_handles_.clear();
    grklog.debug("Worker thread exiting");
  }

protected:
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
  size_t batchSize_ = 30; // Default batch size
  bool stop_ = false;
  std::thread fetchThread_;
  // Retry configuration
  uint32_t maxRetries_ = 3; // Maximum number of retries
  uint32_t retryDelayMs_ = 1000; // Delay between retries in milliseconds

protected:
  TileFetchCallback tileFetchCallback_;
  const TPSEQ_VEC* allTileParts_ = nullptr;
  time_t last_modified_time_ = -1; // Unix timestamp; -1 if unavailable

private:
  CURL_FETCHER_WRITE_CALLBACK tileWriteCallback_;
  ScheduledTileFetch currentTileFetch_;
  ScheduledChunkFetch currentChunkFetch_;
};

inline void TileFetchContext::incrementCompleteCount()
{
  std::atomic_ref<size_t> atomicCount(completeCount_);
  if((atomicCount.fetch_add(1, std::memory_order_seq_cst) + 1) == requests_->size())
  {
    fetcher_->onFetchTilesComplete(shared_from_this(), true);
  }
}

} // namespace grk