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

#include <ctime>

#include "grk_config_private.h"
#include "CurlFetcher.h"
#include "FetchPathParser.h"

#ifdef GRK_ENABLE_LIBCURL

namespace grk
{

class GSFetcher : public CurlFetcher
{
protected:
  void parse(const std::string& path) override
  {
    // Create a non-const copy of path for FetchPathParser
    std::string mutable_path = path;
    ParsedFetchPath parsed;
    if(mutable_path.starts_with("/vsigs/"))
    {
      // Parse /vsigs/ path using FetchPathParser
      FetchPathParser::parseVsiPath(mutable_path, parsed, "vsigs");
      parsed.host = "storage.googleapis.com";
      parsed.port = 443;
    }
    else if(mutable_path.starts_with("https://"))
    {
      // Parse HTTPS path using FetchPathParser
      FetchPathParser::parseHttpsPath(mutable_path, parsed);
    }
    else
    {
      grklog.error("Unsupported URL format for GS: %s", path.c_str());
      throw std::runtime_error("Unsupported URL format for GS");
    }

    grklog.debug("Parsed GS URL - Host: %s, Port: %d, Bucket: %s, Key: %s", parsed.host.c_str(),
                 parsed.port, parsed.bucket.c_str(), parsed.key.c_str());

    // Construct the final URL
    url_ = "https://" + parsed.host + "/" + parsed.bucket + "/" + parsed.key;
    grklog.debug("Parsed GSFetcher URL: %s", url_.c_str());
  }

  void auth(CURL* curl) override
  {
    // Apply parent auth settings (e.g., SSL verification)
    CurlFetcher::auth(curl);

    // Check environment variables for access key and secret
    std::string access_key = auth_.username_;
    std::string secret_key = auth_.password_;

    if(access_key.empty())
    {
      if(const char* key = std::getenv("GS_ACCESS_KEY_ID"))
      {
        access_key = key;
        grklog.debug("Set GS access key = %s", access_key.c_str());
      }
    }
    if(secret_key.empty())
    {
      if(const char* secret = std::getenv("GS_SECRET_ACCESS_KEY"))
      {
        secret_key = secret;
        grklog.debug("Set GS secret key = %s", secret_key.c_str());
      }
    }

    // Apply authentication if both access key and secret are available
    if(!access_key.empty() && !secret_key.empty())
    {
      curl_easy_setopt(curl, CURLOPT_USERNAME, access_key.c_str());
      curl_easy_setopt(curl, CURLOPT_PASSWORD, secret_key.c_str());
      grklog.debug("Applied GS authentication for access key: %s", access_key.c_str());
    }
    else
    {
      grklog.debug("No GS authentication applied (public access or other auth)");
    }
  }

  curl_slist* prepareAuthHeaders(curl_slist* headers) override
  {
    time_t now;
    time(&now);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    std::string date_header = "Date: " + std::string(date_buf);
    headers = curl_slist_append(headers, date_header.c_str());

    return headers;
  }
};

} // namespace grk

#endif