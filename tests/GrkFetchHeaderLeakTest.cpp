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
// still has to free it. this test stands in for the object store with a server
// on the loopback address, then counts every list the decoder builds and every
// list it frees over a run of range requests.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "grok.h"

namespace
{
const uint32_t NUM_SETUPS = 32;
const size_t CONTENT_LENGTH = 8192;
const uint8_t CODESTREAM_MAGIC[] = {0xff, 0x4f, 0xff, 0x51};
const uint8_t FILLER_BYTE = 0xa5;
// one list per request is enough to make the point, but a request the
// decoder decorates carries more, and each of those has to come back too
const char* CUSTOM_HEADERS[] = {"X-Grk-Test-One: first", "X-Grk-Test-Two: second",
                                "X-Grk-Test-Three: third"};
const uint8_t NUM_CUSTOM_HEADERS = sizeof(CUSTOM_HEADERS) / sizeof(CUSTOM_HEADERS[0]);
// a head request for the size and a range request for the identifier bytes
const uint32_t LISTS_PER_SETUP = 2;

std::atomic<uint64_t> listsBuilt{0};
std::atomic<uint64_t> listsFreed{0};

void discardLog(const char*, void*) {}

std::string content(void)
{
  std::string data(CONTENT_LENGTH, (char)FILLER_BYTE);
  memcpy(&data[0], CODESTREAM_MAGIC, sizeof(CODESTREAM_MAGIC));
  return data;
}

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

void serveRequest(int socketDescriptor, const std::string& body)
{
  std::string request = readRequest(socketDescriptor);
  char header[256];
  if(request.starts_with("HEAD"))
  {
    int length = snprintf(header, sizeof(header),
                          "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nAccept-Ranges: "
                          "bytes\r\nConnection: close\r\n\r\n",
                          body.size());
    sendAll(socketDescriptor, header, (size_t)length);
    return;
  }

  size_t rangeStart = 0;
  size_t rangeEnd = body.size() - 1;
  size_t rangeField = request.find("Range: bytes=");
  if(rangeField != std::string::npos)
    sscanf(request.c_str() + rangeField, "Range: bytes=%zu-%zu", &rangeStart, &rangeEnd);
  if(rangeEnd >= body.size())
    rangeEnd = body.size() - 1;
  if(rangeStart > rangeEnd)
    rangeStart = rangeEnd;

  size_t length = rangeEnd - rangeStart + 1;
  int headerLength = snprintf(header, sizeof(header),
                              "HTTP/1.1 206 Partial Content\r\nContent-Length: %zu\r\nContent-"
                              "Range: bytes %zu-%zu/%zu\r\nConnection: close\r\n\r\n",
                              length, rangeStart, rangeEnd, body.size());
  if(sendAll(socketDescriptor, header, (size_t)headerLength))
    sendAll(socketDescriptor, body.data() + rangeStart, length);
}

class LoopbackServer
{
public:
  bool start(void)
  {
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
    if(listen(listenDescriptor_, 16) < 0)
      return false;

    socklen_t addressLength = sizeof(address);
    if(getsockname(listenDescriptor_, (sockaddr*)&address, &addressLength) < 0)
      return false;
    port_ = ntohs(address.sin_port);

    body_ = content();
    thread_ = std::thread([this] { run(); });
    return true;
  }

  void stop(void)
  {
    stopping_ = true;
    wake();
    if(thread_.joinable())
      thread_.join();
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
      if(!stopping_)
        serveRequest(connectionDescriptor, body_);
      close(connectionDescriptor);
    }
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
  std::thread thread_;
};

bool setUpStream(uint16_t port)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  streamParams.max_retry = 1;
  streamParams.connect_timeout = 5;
  streamParams.timeout = 10;
  snprintf(streamParams.file, sizeof(streamParams.file), "http://127.0.0.1:%u/object.j2k", port);
  streamParams.num_custom_headers = NUM_CUSTOM_HEADERS;
  for(uint8_t i = 0; i < NUM_CUSTOM_HEADERS; ++i)
    snprintf(streamParams.custom_headers[i], sizeof(streamParams.custom_headers[i]), "%s",
             CUSTOM_HEADERS[i]);

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    return false;
  grk_object_unref(codec);
  return true;
}
} // namespace

using AppendFunction = void* (*)(void*, const char*);
using FreeFunction = void (*)(void*);

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

int main(void)
{
  grk_msg_handlers handlers = {};
  handlers.info_callback = discardLog;
  handlers.debug_callback = discardLog;
  handlers.trace_callback = discardLog;
  handlers.warn_callback = discardLog;
  handlers.error_callback = discardLog;
  grk_set_msg_handlers(handlers);

  LoopbackServer server;
  if(!server.start())
  {
    fprintf(stderr, "cannot listen on the loopback address\n");
    return 1;
  }

  int status = 0;
  for(uint32_t i = 0; i < NUM_SETUPS && status == 0; ++i)
  {
    if(!setUpStream(server.port()))
    {
      fprintf(stderr, "stream setup %u failed against the loopback server\n", i);
      status = 1;
    }
  }
  server.stop();
  if(status)
    return status;

  uint64_t built = listsBuilt;
  uint64_t freed = listsFreed;
  printf("built %llu header lists, freed %llu, over %u stream setups\n", (unsigned long long)built,
         (unsigned long long)freed, NUM_SETUPS);

  if(built < (uint64_t)NUM_SETUPS * LISTS_PER_SETUP)
  {
    fprintf(stderr, "only %llu header lists built, so the requests never happened\n",
            (unsigned long long)built);
    return 1;
  }
  if(built != freed)
  {
    fprintf(stderr, "%llu header lists were never freed\n", (unsigned long long)(built - freed));
    return 1;
  }

  return 0;
}
