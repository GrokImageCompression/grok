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

// curl only borrows the header list a request is given, so whoever builds one
// still has to free it, and tile data is fetched over range requests that can
// still be open when a codec is dropped. this test stands in for the object
// store with a server on the loopback address that answers the header requests
// and then goes quiet, so the tile requests are still open at shutdown, then
// counts every list the fetcher builds and every list it frees.

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "grok.h"

namespace
{
  const uint32_t IMAGE_WIDTH = 256;
  const uint32_t IMAGE_HEIGHT = 256;
  const uint32_t TILE_WIDTH = 32;
  const uint32_t TILE_HEIGHT = 32;
  const uint16_t NUM_COMPONENTS = 1;
  const uint8_t PRECISION = 8;
  // the fetcher drives its open requests round after round, so a few rounds
  // after the last request went out its batch is fully built
  const uint64_t FETCH_ROUNDS_AFTER_REQUEST = 3;
  // generous enough that a quiet server does not end the requests for us
  const int STALLED_REQUEST_TIMEOUT_SECONDS = 120;
  // long enough for a loopback fetch to get going, short enough to report a
  // path that never fetches instead of hanging
  const int REQUEST_WAIT_SECONDS = 30;

  std::atomic<uint64_t> listsBuilt{0};
  std::atomic<uint64_t> listsFreed{0};
  // counted only once the server has gone quiet, so header traffic is not
  // mistaken for the tile request the codec is dropped on top of
  std::atomic<bool> watchingRequests{false};
  std::atomic<uint64_t> requestsSent{0};
  std::atomic<uint64_t> fetchRoundsSinceRequest{0};
  std::mutex fetchMutex;
  std::condition_variable fetchCondition;

  bool requestIsSettled(void)
  {
    return requestsSent > 0 && fetchRoundsSinceRequest >= FETCH_ROUNDS_AFTER_REQUEST;
  }

  void discardLog(const char*, void*) {}

  bool sendAll(int socketDescriptor, const char* data, size_t length)
  {
    size_t sent = 0;
    while(sent < length)
    {
      ssize_t written = send(socketDescriptor, data + sent, length - sent, MSG_NOSIGNAL);
      if(written <= 0)
        return false;
      sent += (size_t)written;
    }
    return true;
  }

  std::string readRequest(int socketDescriptor)
  {
    std::string request;
    char buffer[1024];
    while(request.find("\r\n\r\n") == std::string::npos)
    {
      ssize_t received = recv(socketDescriptor, buffer, sizeof(buffer), 0);
      if(received <= 0)
        break;
      request.append(buffer, (size_t)received);
    }
    return request;
  }

  class LoopbackServer
  {
  public:
    bool start(const std::string& body)
    {
      body_ = body;
      listenDescriptor_ = socket(AF_INET, SOCK_STREAM, 0);
      if(listenDescriptor_ < 0)
        return false;
      int reuse = 1;
      setsockopt(listenDescriptor_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

      sockaddr_in address = {};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = 0;
      if(bind(listenDescriptor_, (sockaddr*)&address, sizeof(address)) < 0)
        return false;
      if(listen(listenDescriptor_, 128) < 0)
        return false;

      socklen_t addressLength = sizeof(address);
      if(getsockname(listenDescriptor_, (sockaddr*)&address, &addressLength) < 0)
        return false;
      port_ = ntohs(address.sin_port);

      thread_ = std::thread([this] { run(); });
      return true;
    }

    // once quiet, a request is read and then left unanswered until shutdown
    void goQuiet(void)
    {
      quiet_ = true;
    }

    void stop(void)
    {
      stopping_ = true;
      quietCondition_.notify_all();
      wake();
      if(thread_.joinable())
        thread_.join();
      for(auto& connection : connections_)
      {
        if(connection.joinable())
          connection.join();
      }
      close(listenDescriptor_);
    }

    uint16_t port(void) const
    {
      return port_;
    }

  private:
    void run(void)
    {
      while(!stopping_)
      {
        int connectionDescriptor = accept(listenDescriptor_, nullptr, nullptr);
        if(connectionDescriptor < 0)
          continue;
        if(stopping_)
        {
          close(connectionDescriptor);
          continue;
        }
        connections_.emplace_back([this, connectionDescriptor] { serve(connectionDescriptor); });
      }
    }

    void serve(int connectionDescriptor)
    {
      std::string request = readRequest(connectionDescriptor);
      if(quiet_)
      {
        std::unique_lock<std::mutex> lock(quietMutex_);
        quietCondition_.wait(lock, [this] { return stopping_.load(); });
        close(connectionDescriptor);
        return;
      }

      char header[256];
      if(request.starts_with("HEAD"))
      {
        int length = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nAccept-Ranges: "
                              "bytes\r\nConnection: close\r\n\r\n",
                              body_.size());
        sendAll(connectionDescriptor, header, (size_t)length);
        close(connectionDescriptor);
        return;
      }

      size_t rangeStart = 0;
      size_t rangeEnd = body_.size() - 1;
      size_t rangeField = request.find("Range: bytes=");
      if(rangeField != std::string::npos)
        sscanf(request.c_str() + rangeField, "Range: bytes=%zu-%zu", &rangeStart, &rangeEnd);
      if(rangeEnd >= body_.size())
        rangeEnd = body_.size() - 1;
      if(rangeStart > rangeEnd)
        rangeStart = rangeEnd;

      size_t length = rangeEnd - rangeStart + 1;
      int headerLength = snprintf(header, sizeof(header),
                                  "HTTP/1.1 206 Partial Content\r\nContent-Length: %zu\r\nContent-"
                                  "Range: bytes %zu-%zu/%zu\r\nConnection: close\r\n\r\n",
                                  length, rangeStart, rangeEnd, body_.size());
      if(sendAll(connectionDescriptor, header, (size_t)headerLength))
        sendAll(connectionDescriptor, body_.data() + rangeStart, length);
      close(connectionDescriptor);
    }

    // the accept call blocks, so closing time needs one more connection
    void wake(void)
    {
      int waker = socket(AF_INET, SOCK_STREAM, 0);
      if(waker < 0)
        return;
      sockaddr_in address = {};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(port_);
      connect(waker, (sockaddr*)&address, sizeof(address));
      close(waker);
    }

    int listenDescriptor_ = -1;
    uint16_t port_ = 0;
    std::string body_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> quiet_{false};
    std::mutex quietMutex_;
    std::condition_variable quietCondition_;
    std::thread thread_;
    std::vector<std::thread> connections_;
  };

  grk_image* makeImage(void)
  {
    grk_image_comp params[NUM_COMPONENTS] = {};
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      params[c].dx = 1;
      params[c].dy = 1;
      params[c].w = IMAGE_WIDTH;
      params[c].h = IMAGE_HEIGHT;
      params[c].prec = PRECISION;
      params[c].sgnd = false;
    }
    grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_GRAY, true);
    if(!image)
      return nullptr;
    auto* data = static_cast<int32_t*>(image->comps[0].data);
    if(!data)
    {
      grk_object_unref(&image->obj);
      return nullptr;
    }
    uint32_t stride = image->comps[0].stride;
    for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
      for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
        data[(size_t)y * stride + x] = (int32_t)((x * 7 + y * 13) & 0xFF);
    return image;
  }

  // tile fetching is driven off the TLM marker, so the codestream needs one
  bool compress(const std::string& path)
  {
    grk_image* image = makeImage();
    if(!image)
      return false;

    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.tile_size_on = true;
    parameters.t_width = TILE_WIDTH;
    parameters.t_height = TILE_HEIGHT;
    parameters.write_tlm = true;

    grk_stream_params streamParams = {};
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
    bool ok = false;
    if(codec)
    {
      ok = grk_compress(codec, nullptr) != 0;
      grk_object_unref(codec);
    }
    grk_object_unref(&image->obj);
    return ok;
  }

  std::string readFile(const std::string& path)
  {
    std::string contents;
    FILE* file = fopen(path.c_str(), "rb");
    if(!file)
      return contents;
    char buffer[8192];
    size_t read = 0;
    while((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
      contents.append(buffer, read);
    fclose(file);
    return contents;
  }

  bool dropCodecMidFetch(LoopbackServer& server)
  {
    grk_decompress_parameters params = {};
    params.asynchronous = true;
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    streamParams.max_retry = 1;
    streamParams.connect_timeout = STALLED_REQUEST_TIMEOUT_SECONDS;
    streamParams.timeout = STALLED_REQUEST_TIMEOUT_SECONDS;
    snprintf(streamParams.file, sizeof(streamParams.file), "http://127.0.0.1:%u/object.j2k",
             server.port());

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "grk_decompress_init failed against the loopback server\n");
      return false;
    }
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
    {
      fprintf(stderr, "grk_decompress_read_header failed against the loopback server\n");
      grk_object_unref(codec);
      return false;
    }

    server.goQuiet();
    watchingRequests = true;
    grk_decompress(codec, nullptr);

    std::unique_lock<std::mutex> lock(fetchMutex);
    fetchCondition.wait_for(lock, std::chrono::seconds(REQUEST_WAIT_SECONDS),
                            [] { return requestIsSettled(); });
    lock.unlock();

    grk_object_unref(codec);
    return true;
  }
} // namespace

using AppendFunction = void* (*)(void*, const char*);
using FreeFunction = void (*)(void*);
using AddHandleFunction = int (*)(void*, void*);
using PerformFunction = int (*)(void*, int*);

extern "C" void* curl_slist_append(void* list, const char* data)
{
  static AppendFunction system = (AppendFunction)dlsym(RTLD_NEXT, "curl_slist_append");
  if(!list)
    listsBuilt++;
  return system(list, data);
}

extern "C" void curl_slist_free_all(void* list)
{
  static FreeFunction system = (FreeFunction)dlsym(RTLD_NEXT, "curl_slist_free_all");
  if(list)
    listsFreed++;
  system(list);
}

extern "C" int curl_multi_add_handle(void* multi, void* easy)
{
  static AddHandleFunction system = (AddHandleFunction)dlsym(RTLD_NEXT, "curl_multi_add_handle");
  int result = system(multi, easy);
  std::lock_guard<std::mutex> lock(fetchMutex);
  if(watchingRequests)
  {
    requestsSent++;
    fetchRoundsSinceRequest = 0;
  }
  return result;
}

extern "C" int curl_multi_perform(void* multi, int* running)
{
  static PerformFunction system = (PerformFunction)dlsym(RTLD_NEXT, "curl_multi_perform");
  int result = system(multi, running);
  std::lock_guard<std::mutex> lock(fetchMutex);
  fetchRoundsSinceRequest++;
  fetchCondition.notify_all();
  return result;
}

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);
  grk_msg_handlers handlers = {};
  handlers.info_callback = discardLog;
  handlers.debug_callback = discardLog;
  handlers.trace_callback = discardLog;
  handlers.warn_callback = discardLog;
  handlers.error_callback = discardLog;
  grk_set_msg_handlers(handlers);

  std::string path = "fetch_shutdown_leak_test.j2k";
  if(!compress(path))
  {
    fprintf(stderr, "could not build the source codestream\n");
    grk_deinitialize();
    return 1;
  }
  std::string body = readFile(path);
  remove(path.c_str());
  if(body.empty())
  {
    fprintf(stderr, "the source codestream is empty\n");
    grk_deinitialize();
    return 1;
  }

  LoopbackServer server;
  if(!server.start(body))
  {
    fprintf(stderr, "cannot listen on the loopback address\n");
    grk_deinitialize();
    return 1;
  }

  bool dropped = dropCodecMidFetch(server);
  server.stop();
  grk_deinitialize();
  if(!dropped)
    return 1;

  uint64_t builtLists = listsBuilt;
  uint64_t freedLists = listsFreed;
  uint64_t openRequests = requestsSent;
  printf("built %llu header lists, freed %llu, over %llu tile range requests\n",
         (unsigned long long)builtLists, (unsigned long long)freedLists,
         (unsigned long long)openRequests);

  if(openRequests == 0)
  {
    fprintf(stderr, "no tile range request was open when the codec was dropped\n");
    return 1;
  }
  if(builtLists != freedLists)
  {
    fprintf(stderr, "%llu header lists were never freed\n",
            (unsigned long long)(builtLists - freedLists));
    return 1;
  }

  return 0;
}
