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

} // namespace grk