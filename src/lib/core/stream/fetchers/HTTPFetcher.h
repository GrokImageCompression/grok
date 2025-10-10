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

#include "grk_config_private.h"
#include "CurlFetcher.h"
#include "FetchPathParser.h"
#include <ctime>

#ifdef GRK_ENABLE_LIBCURL

namespace grk
{

class HTTPFetcher : public CurlFetcher
{
protected:
  void parse(const std::string& path) override
  {
    // Create a non-const copy of path for FetchPathParser
    std::string mutable_path = path;
    if(mutable_path.starts_with("/vsicurl/"))
    {
      ParsedFetchPath parsed;
      FetchPathParser::parseVsiPath(mutable_path, parsed, "vsicurl");
      url_ = "https://" + parsed.bucket + "/" + parsed.key;
      if(!url_.starts_with("http://") && !url_.starts_with("https://"))
      {
        grklog.error("Invalid /vsicurl/ URL: must start with http:// or https://: %s",
                     url_.c_str());
        throw std::runtime_error("Invalid /vsicurl/ URL: must start with http:// or https://");
      }
      grklog.debug("Parsed /vsicurl/ URL: %s", url_.c_str());
    }
    else if(mutable_path.starts_with("http://") || mutable_path.starts_with("https://"))
    {
      url_ = mutable_path; // Use the URL as-is
      grklog.debug("Parsed HTTP/HTTPS URL: %s", url_.c_str());
    }
    else
    {
      grklog.error(
          "Unsupported URL format for HTTPFetcher; must be http://, https://, or /vsicurl/: %s",
          path.c_str());
      throw std::runtime_error(
          "Unsupported URL format for HTTPFetcher; must be http://, https://, or /vsicurl/");
    }
  }

  void auth(CURL* curl) override
  {
    // Apply parent auth settings (e.g., SSL verification)
    CurlFetcher::auth(curl);

    // Check environment variables for username and password
    std::string username = auth_.username_;
    std::string password = auth_.password_;
    if(username.empty() && password.empty())
    {
      if(const char* userpwd = std::getenv("GDAL_HTTP_USERPWD"))
      {
        std::string userpwd_str(userpwd);
        size_t colon_pos = userpwd_str.find(':');
        if(colon_pos != std::string::npos)
        {
          username = userpwd_str.substr(0, colon_pos);
          password = userpwd_str.substr(colon_pos + 1);
          grklog.debug("Set HTTP username = %s and password from GDAL_HTTP_USERPWD",
                       username.c_str());
        }
      }
    }

    // Apply username and password if available
    if(!username.empty() && !password.empty())
    {
      curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
      curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
      grklog.debug("Applied HTTP basic authentication for username: %s", username.c_str());
    }

    // GDAL /vsicurl/ supports custom headers via GDAL_HTTP_HEADER_FILE
    if(const char* header_file = std::getenv("GDAL_HTTP_HEADER_FILE"))
    {
      // Note: Simplified; actual implementation would read headers from the file
      grklog.debug("GDAL_HTTP_HEADER_FILE set to %s (not fully implemented)", header_file);
    }

    // Log custom header and bearer token usage
    if(!auth_.custom_header_.empty())
    {
      grklog.debug("Using custom header: %s", auth_.custom_header_.c_str());
    }
    if(!auth_.bearer_token_.empty())
    {
      grklog.debug("Using bearer token: %s", auth_.bearer_token_.c_str());
    }
  }

  curl_slist* prepareAuthHeaders(curl_slist* headers) override
  {
    // Add custom header from FetchAuth if provided
    if(!auth_.custom_header_.empty())
    {
      headers = curl_slist_append(headers, auth_.custom_header_.c_str());
    }

    // Add Authorization header for bearer token if provided
    if(!auth_.bearer_token_.empty())
    {
      std::string auth_header = "Authorization: Bearer " + auth_.bearer_token_;
      headers = curl_slist_append(headers, auth_header.c_str());
    }

    return headers;
  }
};

} // namespace grk

#endif