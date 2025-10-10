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

#include "grk_apps_config.h"
#include "grok.h"
#include "IFileIO.h"
#include "FileStandardIO.h"

#include <cstdint>

struct FileOrchestratorIO
{
  FileOrchestratorIO(void);
  void setMaxPooledRequests(uint32_t maxRequests);
  void registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
                                  void* user_data);
  grk_io_callback getIOReclaimCallback(void);
  void* getIOReclaimUserData(void);
#ifndef _WIN32
  int getFd(void);
#endif
  bool open(const std::string& name, const std::string& mode, bool asynch);
  bool close(void);
  size_t write(uint8_t* buf, size_t size);
  uint64_t seek(int64_t off, int32_t whence);
  uint32_t getNumPooledRequests(void);
  uint64_t getOffset(void);
  void incrementPooled(void);
  bool allPooledRequestsComplete(void);

private:
#ifndef _WIN32
  int getMode(std::string mode);
  int fd_;
#else
  FileStandardIO fileStreamIO;
#endif
  uint32_t numPooledRequests_;
  // used to detect when library-orchestrated encode is complete
  uint32_t max_pooled_requests_;
  bool asynchActive_;
  uint64_t off_;
  grk_io_callback reclaim_callback_;
  void* reclaim_user_data_;
  std::string filename_;
  std::string mode_;
};
