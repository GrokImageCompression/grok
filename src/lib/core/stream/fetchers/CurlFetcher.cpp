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

#include "CurlFetcher.h"

#ifdef GRK_ENABLE_LIBCURL

#include "Logger.h"
#include "TPFetchSeq.h"

namespace grk
{

// Tile write callback — copies data into TPFetch::data_ (zero-copy to tile buffer)
static size_t tileWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
  size_t total_size = size * nmemb;
  auto result = static_cast<FetchResult*>(userp);
  auto ctx = std::static_pointer_cast<TileFetchContext>(result->ctx_);
  if(ctx)
  {
    auto& tpseq = (*ctx->requests_)[result->requestIndex_];
    tpseq->copy(static_cast<uint8_t*>(contents), total_size);
    if(tpseq->fetchOffset_ == tpseq->length_)
    {
      ctx->callback_(result->requestIndex_, ctx.get());
      ctx->incrementCompleteCount();
    }
  }
  else
  {
    result->data_.insert(result->data_.end(), static_cast<const uint8_t*>(contents),
                         static_cast<const uint8_t*>(contents) + total_size);
  }
  return total_size;
}

// Chunk write callback — accumulates into result data, delivers to ChunkBuffer when complete
static size_t chunkWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
  size_t total_size = size * nmemb;
  auto* res = static_cast<FetchResult*>(userp);
  res->data_.insert(res->data_.end(), static_cast<uint8_t*>(contents),
                    static_cast<uint8_t*>(contents) + total_size);
  auto ctx = std::static_pointer_cast<ChunkContext>(res->ctx_);
  auto& req = (*ctx->requests_)[res->requestIndex_];
  if(res->data_.size() == req.length_)
  {
    ctx->chunkBuffer_->add(static_cast<ChunkBuffer<>::index_type>(res->requestIndex_),
                           res->data_.data(), req.length_);
    res->data_.clear();
    res->data_.shrink_to_fit();
  }

  return total_size;
}

CurlFetcher::CurlFetcher(void) : tileWriteCallback_(tileWriteCallback)
{
  curl_global_init(CURL_GLOBAL_ALL);
  multi_handle_ = curl_multi_init();
  if(!multi_handle_)
    throw std::runtime_error("Failed to initialize CURL multi handle");
  curl_multi_setopt(multi_handle_, CURLMOPT_MAX_TOTAL_CONNECTIONS, 100L);
  curl_multi_setopt(multi_handle_, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  fetchThread_ = std::thread(&CurlFetcher::fetchWorker, this);
}

CurlFetcher::~CurlFetcher()
{
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  queue_cv_.notify_all(); // Wake worker from wait on queue_cv_
  throttleCV_.notify_all(); // Wake worker from throttle wait
  if(fetchThread_.joinable())
    fetchThread_.join();
  if(multi_handle_)
    curl_multi_cleanup(multi_handle_);
  curl_global_cleanup();
}

void CurlFetcher::setFetchThrottle(std::function<bool()> throttle)
{
  std::lock_guard<std::mutex> lock(throttleMutex_);
  fetchThrottle_ = std::move(throttle);
}

void CurlFetcher::notifyThrottleRelease()
{
  throttleCV_.notify_one();
}

void CurlFetcher::setBatchSize(size_t batchSize)
{
  batchSize_ = std::max<size_t>(1, batchSize);
}

void CurlFetcher::init(const std::string& path, const FetchAuth& auth)
{
  auth_ = auth;
  if(auth_.max_retry_ > 0)
    maxRetries_ = auth_.max_retry_;
  if(auth_.retry_delay_ > 0)
    retryDelayMs_ = auth_.retry_delay_ * 1000;
  if(auth_.fetch_batch_size_ > 0)
    batchSize_ = auth_.fetch_batch_size_;
  parse(path);
  fetch_total_size();
}

size_t CurlFetcher::read(uint8_t* buffer, size_t numBytes)
{
  if(current_offset_ + numBytes > total_size_)
  {
    grklog.error("Read %zu bytes at offset %llu exceeds total size %llu", numBytes,
                 current_offset_, total_size_);
    return 0;
  }

  FetchResult result;
  auto curl = configureHandle(current_offset_, current_offset_ + numBytes - 1, result,
                              tileWriteCallback_);
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

bool CurlFetcher::seek(uint64_t offset)
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

std::future<bool> CurlFetcher::fetchTiles(const TPSEQ_VEC& allTileParts,
                                           const std::set<uint16_t>& slated, void* user_data,
                                           TileFetchCallback callback)
{
  // 1. cache fetch data
  {
    std::lock_guard<std::mutex> lock(fetch_mutex_);
    allTileParts_ = &allTileParts;
    if(!user_data_)
      user_data_ = user_data;
    if(callback)
      tileFetchCallback_ = callback;
  }

  // 2. queue fetch
  FetchJob job{std::set<uint16_t>(slated)};
  std::future<bool> future = job.promise_.get_future();
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    tile_fetch_queue_.push(std::move(job));
  }
  queue_cv_.notify_one();
  grklog.debug("Queued tile fetch job, queue size: %zu", tile_fetch_queue_.size());

  return future;
}

void CurlFetcher::onFetchTilesComplete(std::shared_ptr<TileFetchContext> context, bool success)
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

void CurlFetcher::fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer)
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
  fetchChunks(chunkBuffer, requests);
}

void CurlFetcher::fetchChunks(std::shared_ptr<ChunkBuffer<>> chunkBuffer,
                               std::shared_ptr<std::vector<ChunkRequest>> requests)
{
  ChunkTask task(chunkBuffer, requests);

  for(size_t i = 0; i < task.requests_->size(); ++i)
  {
    auto& req = (*task.requests_)[i];
    if(req.end_ < req.offset_ || req.end_ >= total_size_)
    {
      grklog.error("Invalid range %llu-%llu for ID %u (total size: %llu)", req.offset_, req.end_,
                   req.requestIndex_, total_size_);
    }
  }

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    chunk_fetch_queue_.push(std::move(task));
  }
  queue_cv_.notify_one();
  grklog.debug("Queued chunk fetch task with %zu requests", requests->size());
}

// Directory listing
std::vector<std::string> CurlFetcher::listDirectory(const std::string& path)
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
    curl_initiate_retry(curl);
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
      break;
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
bool CurlFetcher::getMetadata(const std::string& path,
                               std::map<std::string, std::string>& metadata)
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
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    auth(curl);
    curl_initiate_retry(curl);
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
      break;
    }
  } while(true);

  if(success)
  {
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

void CurlFetcher::fetchError(FetchResult* result)
{
  auto ctx = std::static_pointer_cast<TileFetchContext>(result->ctx_);
  if(ctx && ctx->fetcher_)
    ctx->fetcher_->onFetchTilesComplete(ctx, false);
}

void CurlFetcher::auth(CURL* curl)
{
  if(EnvVarManager::test_bool("GRK_CURL_ALLOW_INSECURE") || auth_.s3_allow_insecure_)
  {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if(!auth_.username_.empty())
    curl_easy_setopt(curl, CURLOPT_USERNAME, auth_.username_.c_str());
  if(!auth_.password_.empty())
    curl_easy_setopt(curl, CURLOPT_PASSWORD, auth_.password_.c_str());

  // Cookies (CURLOPT_COOKIE / COOKIEFILE / COOKIEJAR)
  if(!auth_.cookie_.empty())
    curl_easy_setopt(curl, CURLOPT_COOKIE, auth_.cookie_.c_str());
  if(!auth_.cookie_file_.empty())
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, auth_.cookie_file_.c_str());
  if(!auth_.cookie_jar_.empty())
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, auth_.cookie_jar_.c_str());

  // .netrc (CURLOPT_NETRC / CURLOPT_NETRC_FILE)
  if(auth_.netrc_)
    curl_easy_setopt(curl, CURLOPT_NETRC, (long)CURL_NETRC_REQUIRED);
  if(!auth_.netrc_file_.empty())
    curl_easy_setopt(curl, CURLOPT_NETRC_FILE, auth_.netrc_file_.c_str());

  // Proxy (shared by both HTTP and S3 fetchers)
  if(!auth_.proxy_.empty())
  {
    curl_easy_setopt(curl, CURLOPT_PROXY, auth_.proxy_.c_str());
    if(!auth_.proxy_userpwd_.empty())
      curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, auth_.proxy_userpwd_.c_str());
    curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
  }
  else if(auto proxy = EnvVarManager::get("GRK_CURL_PROXY"))
  {
    curl_easy_setopt(curl, CURLOPT_PROXY, proxy->c_str());
    if(auto proxyUserPwd = EnvVarManager::get("GRK_CURL_PROXYUSERPWD"))
      curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxyUserPwd->c_str());
    if(EnvVarManager::get("GRK_CURL_PROXYAUTH"))
      curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
  }

  // User agent
  if(!auth_.user_agent_.empty())
    curl_easy_setopt(curl, CURLOPT_USERAGENT, auth_.user_agent_.c_str());

  // Timeouts
  long connect_timeout = auth_.connect_timeout_ > 0 ? auth_.connect_timeout_ : 10L;
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
}

curl_slist* CurlFetcher::configureHeaders(const std::string& range)
{
  struct curl_slist* headers = nullptr;
  headers = prepareAuthHeaders(headers);
  if(!range.empty())
  {
    headers = curl_slist_append(headers, range.c_str());
  }
  return headers;
}

void CurlFetcher::fetch_total_size()
{
  CURL* curl = curl_easy_init();
  if(!curl)
    throw std::runtime_error("Failed to initialize CURL easy handle for HEAD");

  curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
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

CURL* CurlFetcher::configureHandle(uint64_t offset, uint64_t end, FetchResult& result,
                                    CURL_FETCHER_WRITE_CALLBACK callback)
{
  CURL* curl = curl_easy_init();
  if(!curl)
    throw std::runtime_error("Failed to initialize CURL easy handle");

  curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
  curl_initiate_retry(curl);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(curl, CURLOPT_PIPEWAIT, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
  curl_easy_setopt(curl, CURLOPT_PRIVATE, &result);

  auth(curl);

  std::string range = "Range: bytes=" + std::to_string(offset) + "-" + std::to_string(end);
  auto headers = configureHeaders(range);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  return curl;
}

std::shared_ptr<TileFetchContext> CurlFetcher::scheduleTileFetch(const std::set<uint16_t>& slated)
{
  auto requests = std::make_shared<TPFetchSeq>();
  auto tilePartFetchByTile =
      std::make_shared<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>>();
  TPFetchSeq::genCollections(allTileParts_, slated, requests, tilePartFetchByTile);

  auto results = std::make_shared<std::vector<FetchResult>>(requests->size());

  auto ctx = std::make_shared<TileFetchContext>(requests, user_data_, tilePartFetchByTile,
                                                tileFetchCallback_, this);

  auto batch = std::make_unique<TileRequestBatch>(requests);
  currentFetch_ = ScheduledFetch(ctx, std::move(batch), results);

  // Set context on all results
  for(auto& r : *results)
    r.ctx_ = ctx;

  return scheduleNextBatch(tileWriteCallback_) ? ctx : nullptr;
}

bool CurlFetcher::scheduleChunkFetch(
    std::shared_ptr<ChunkContext> ctx, std::shared_ptr<std::vector<ChunkRequest>> requests,
    std::shared_ptr<std::vector<FetchResult>> results,
    std::shared_ptr<std::vector<std::promise<FetchResult>>> promises)
{
  auto batch = std::make_unique<ChunkRequestBatch>(requests);
  currentFetch_ = ScheduledFetch(ctx, std::move(batch), results, promises);
  return scheduleNextBatch(chunkWriteCallback);
}

bool CurlFetcher::scheduleNextBatch(CURL_FETCHER_WRITE_CALLBACK callback)
{
  if(!currentFetch_.requests_ || !currentFetch_.requests_->hasMore())
    return true;

  // Back pressure: if a throttle is set, skip scheduling when the
  // downstream pipeline is backlogged.
  {
    std::lock_guard<std::mutex> lock(throttleMutex_);
    if(fetchThrottle_ && !fetchThrottle_())
      return true;
  }

  size_t activeRequests = currentFetch_.scheduled_ - currentFetch_.completed_;
  size_t remainingBatch = batchSize_ > activeRequests ? batchSize_ - activeRequests : 0;
  size_t remainingRequests = currentFetch_.requests_->remaining();
  size_t requestsToSchedule = std::min(remainingBatch, remainingRequests);

  for(size_t i = 0; i < requestsToSchedule && currentFetch_.requests_->hasMore(); ++i)
  {
    auto [offset, end] = currentFetch_.requests_->next();
    if(end >= this->total_size_)
    {
      grklog.warn("Range %llu-%llu exceeds total size %llu", offset, end, total_size_);
      end = this->total_size_ - 1;
    }
    auto& res = (*currentFetch_.results_)[currentFetch_.scheduled_];
    res.requestIndex_ = currentFetch_.scheduled_;
    if(!res.ctx_)
      res.ctx_ = currentFetch_.ctx_;
    CURL* handle = configureHandle(offset, end, res, callback);
    CURLMcode ret = curl_multi_add_handle(multi_handle_, handle);
    if(ret != CURLM_OK)
    {
      grklog.error("curl_multi_add_handle failed: %s", curl_multi_strerror(ret));
      curl_easy_cleanup(handle);
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(active_handles_mutex_);
      active_handles_[handle] = currentFetch_.scheduled_;
    }
    grklog.debug("Added fetch range request: %llu-%llu (index %zu)", offset, end,
                 currentFetch_.scheduled_);
    currentFetch_.scheduled_++;
  }
  return true;
}

void CurlFetcher::retryRequest(FetchResult* result, uint64_t offset, uint64_t end,
                                CURL_FETCHER_WRITE_CALLBACK callback,
                                std::function<void()> onFatalError)
{
  result->retryCount_++;
  grklog.warn("Retrying request %zu (retry %u/%u)", result->requestIndex_, result->retryCount_,
              maxRetries_);

  result->data_.clear();
  result->responseCode_ = 0;
  result->success_ = false;

  // For tile retries, reset the TPFetch write offset so data is re-received from scratch
  if(!currentFetch_.promises_)
  {
    auto ctx = std::static_pointer_cast<TileFetchContext>(result->ctx_);
    if(ctx)
    {
      auto& tpseq = (*ctx->requests_)[result->requestIndex_];
      tpseq->fetchOffset_ = 0;
    }
  }

  if(end >= total_size_)
    end = total_size_ - 1;

  CURL* handle = configureHandle(offset, end, *result, callback);
  CURLMcode ret = curl_multi_add_handle(multi_handle_, handle);
  if(ret != CURLM_OK)
  {
    grklog.error("Retry curl_multi_add_handle failed: %s", curl_multi_strerror(ret));
    curl_easy_cleanup(handle);
    onFatalError();
  }
  else
  {
    std::lock_guard<std::mutex> lock(active_handles_mutex_);
    active_handles_[handle] = result->requestIndex_;
    grklog.debug("Rescheduled retry %u: %llu-%llu (index %zu)", result->retryCount_, offset, end,
                 result->requestIndex_);
  }
}

void CurlFetcher::curl_initiate_retry(CURL* curl)
{
  long timeout = auth_.timeout_ > 0 ? auth_.timeout_ : 30L;
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
}

bool CurlFetcher::shouldRetry(const FetchResult& result, CURLcode curl_code) const
{
  if(result.retryCount_ >= maxRetries_)
    return false;

  bool isCurlError = curl_code != CURLE_OK;
  bool isHttpError = result.responseCode_ != 206 && result.responseCode_ != 0;

  return isCurlError || isHttpError;
}

std::pair<uint64_t, uint64_t> CurlFetcher::getRequestRange(const FetchResult& result) const
{
  if(!currentFetch_.promises_)
  {
    // Tile path — look up from TileFetchContext
    auto ctx = std::static_pointer_cast<TileFetchContext>(result.ctx_);
    auto& req = (*ctx->requests_)[result.requestIndex_];
    return {req->offset_, req->offset_ + req->length_ - 1};
  }
  else
  {
    // Chunk path — look up from ChunkContext
    auto ctx = std::static_pointer_cast<ChunkContext>(result.ctx_);
    auto& req = (*ctx->requests_)[result.requestIndex_];
    return {req.offset_, req.end_};
  }
}

void CurlFetcher::fetchWorker()
{
  CURL_FETCHER_WRITE_CALLBACK activeCallback_ = nullptr;

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
      activeCallback_ = tileWriteCallback_;
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
      activeCallback_ = chunkWriteCallback;
      for(auto& task : chunk_tasks_to_process)
      {
        auto requests = task.requests_;
        auto results = std::make_shared<std::vector<FetchResult>>(requests->size());
        auto promises =
            std::make_shared<std::vector<std::promise<FetchResult>>>(std::move(task.promises_));
        auto ctx = std::make_shared<ChunkContext>(task.chunkBuffer_, requests);
        for(size_t i = 0; i < results->size(); ++i)
        {
          (*results)[i] = FetchResult((*requests)[i].requestIndex_);
          (*results)[i].ctx_ = ctx;
        }
        if(!scheduleChunkFetch(ctx, requests, results, promises))
        {
          for(size_t i = 0; i < promises->size(); ++i)
          {
            (*promises)[i].set_value((*results)[i]);
          }
        }
      }
    }

    int still_running = 0;
    auto ret = curl_multi_perform(multi_handle_, &still_running);
    if(ret != CURLM_OK)
    {
      grklog.error("curl_multi_perform failed: %s", curl_multi_strerror(ret));
      // Fail all active tile jobs
      {
        std::lock_guard<std::mutex> lock(active_jobs_mutex_);
        for(auto& job : active_jobs_)
          job.second.set_value(false);
        active_jobs_.clear();
      }
      // Fail all active chunk promises
      {
        std::lock_guard<std::mutex> lock(active_handles_mutex_);
        if(currentFetch_.promises_)
        {
          for(size_t i = 0; i < currentFetch_.promises_->size(); ++i)
          {
            FetchResult res(i);
            res.success_ = false;
            (*currentFetch_.promises_)[i].set_value(res);
          }
        }
        for(auto& [handle, idx] : active_handles_)
        {
          curl_multi_remove_handle(multi_handle_, handle);
          curl_easy_cleanup(handle);
        }
        active_handles_.clear();
      }
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
        auto* result = static_cast<FetchResult*>(userp);

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result->responseCode_);
        result->success_ = (msg->data.result == CURLE_OK && result->responseCode_ == 206);

        size_t idx = 0;
        {
          std::lock_guard<std::mutex> lock(active_handles_mutex_);
          auto it = active_handles_.find(curl);
          if(it != active_handles_.end())
          {
            idx = it->second;
            active_handles_.erase(it);
          }
        }

        curl_multi_remove_handle(multi_handle_, curl);
        curl_easy_cleanup(curl);

        if(!result->success_)
        {
          if(shouldRetry(*result, msg->data.result))
          {
            auto [offset, end] = getRequestRange(*result);
            grklog.warn("Fetch request %zu failed (HTTP %ld, CURL %s), retrying...",
                        result->requestIndex_, result->responseCode_,
                        curl_easy_strerror(msg->data.result));
            auto callback = activeCallback_;
            retryRequest(result, offset, end, callback, [this, result, idx]() {
              // Fatal retry failure
              if(!currentFetch_.promises_)
                fetchError(result);
              else if(idx < currentFetch_.promises_->size())
                (*currentFetch_.promises_)[idx].set_value(*result);
            });
            continue; // Don't count as completed — retry is in progress
          }

          grklog.error("Fetch request %zu failed: %s, HTTP %ld (no more retries)",
                       result->requestIndex_, curl_easy_strerror(msg->data.result),
                       result->responseCode_);
          // For tile fetches, signal error
          if(!currentFetch_.promises_)
            fetchError(result);
        }
        else
        {
          grklog.debug("Fetch request %zu completed", result->requestIndex_);
        }

        // Set promise for chunk fetches
        if(currentFetch_.promises_ && idx < currentFetch_.promises_->size())
        {
          (*currentFetch_.promises_)[idx].set_value(*result);
        }

        currentFetch_.completed_++;

        // Schedule next batch when half the current batch is complete
        if(currentFetch_.scheduled_ > currentFetch_.completed_ &&
           currentFetch_.completed_ >= batchSize_ / 2 && currentFetch_.requests_ &&
           currentFetch_.requests_->hasMore())
        {
          grklog.debug("Half of batch (%zu) completed, scheduling next batch", batchSize_ / 2);
          if(activeCallback_)
            scheduleNextBatch(activeCallback_);
        }
      }
    }

    if(still_running > 0)
    {
      grklog.trace("Still running: %d requests", still_running);
    }
    else
    {
      // Try to resume scheduling if we were previously throttled
      bool throttled = false;
      if(currentFetch_.requests_ && currentFetch_.requests_->hasMore() && activeCallback_)
      {
        {
          std::lock_guard<std::mutex> tlock(throttleMutex_);
          throttled = fetchThrottle_ && !fetchThrottle_();
        }
        if(!throttled)
          scheduleNextBatch(activeCallback_);
      }

      // If throttled with no in-flight requests, wait for the consumer
      // to release back pressure before continuing
      if(throttled)
      {
        std::unique_lock<std::mutex> tlock(throttleMutex_);
        throttleCV_.wait_for(tlock, std::chrono::milliseconds(100),
                             [this] { return stop_ || !fetchThrottle_ || fetchThrottle_(); });
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
  }

  std::lock_guard<std::mutex> lock(active_jobs_mutex_);
  for(auto& job : active_jobs_)
    job.second.set_value(false);
  active_jobs_.clear();

  std::lock_guard<std::mutex> lock2(active_handles_mutex_);
  if(currentFetch_.promises_)
  {
    for(auto& [handle, idx] : active_handles_)
    {
      FetchResult resp(idx);
      resp.success_ = false;
      if(idx < currentFetch_.promises_->size())
        (*currentFetch_.promises_)[idx].set_value(resp);
      curl_multi_remove_handle(multi_handle_, handle);
      curl_easy_cleanup(handle);
    }
  }
  active_handles_.clear();
  grklog.debug("Worker thread exiting");
}

} // namespace grk

#endif // GRK_ENABLE_LIBCURL

#include "TPFetchSeq.h"

namespace grk
{

void TileFetchContext::incrementCompleteCount()
{
  std::atomic_ref<size_t> atomicCount(completeCount_);
  if((atomicCount.fetch_add(1, std::memory_order_seq_cst) + 1) == requests_->size())
  {
    fetcher_->onFetchTilesComplete(shared_from_this(), true);
  }
}

} // namespace grk
