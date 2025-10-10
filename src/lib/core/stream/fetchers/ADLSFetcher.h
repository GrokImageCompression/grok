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

class ADLSFetcher : public CurlFetcher
{
protected:
  void parse(const std::string& path) override
  {
    // Create a non-const copy of path for FetchPathParser
    std::string mutable_path = path;
    ParsedFetchPath parsed;
    if(mutable_path.starts_with("/vsiadls/"))
    {
      // Parse /vsiadls/ path using FetchPathParser
      std::string account;
      if(!auth_.username_.empty())
      {
        account = auth_.username_;
        grklog.debug("Using auth-provided account for vsiadls: %s", account.c_str());
      }
      else
      {
        grklog.error("No Azure account provided for /vsiadls/ path");
        throw std::runtime_error("No Azure account provided for /vsiadls/ path");
      }
      FetchPathParser::parseVsiPath(mutable_path, parsed, "vsiadls");
      parsed.host = account + ".dfs.core.windows.net";
      parsed.port = 443;
    }
    else if(mutable_path.starts_with("https://"))
    {
      // Parse HTTPS path using FetchPathParser
      FetchPathParser::parseHttpsPath(mutable_path, parsed);
      if(!parsed.host.ends_with(".dfs.core.windows.net"))
      {
        grklog.error("Invalid ADLS HTTPS URL: host must end with .dfs.core.windows.net: %s",
                     parsed.host.c_str());
        throw std::runtime_error(
            "Invalid ADLS HTTPS URL: host must end with .dfs.core.windows.net");
      }
    }
    else
    {
      grklog.error("Unsupported URL format for ADLS: %s", path.c_str());
      throw std::runtime_error("Unsupported URL format for ADLS");
    }

    grklog.debug("Parsed ADLS URL - Host: %s, Port: %d, Filesystem: %s, Path: %s",
                 parsed.host.c_str(), parsed.port, parsed.bucket.c_str(), parsed.key.c_str());

    // Construct the base URL without SAS token
    url_ = "https://" + parsed.host + "/" + parsed.bucket + "/" + parsed.key;
    grklog.debug("Parsed ADLSFetcher URL: %s", url_.c_str());
  }

  void auth(CURL* curl) override
  {
    // Apply parent auth settings (e.g., SSL verification)
    CurlFetcher::auth(curl);

    // ADLS Gen2 authentication logic
    std::string final_url = url_;
    std::string account = auth_.username_;
    std::string secret = auth_.password_;

    // Check environment variables for account and key/SAS token
    if(account.empty())
    {
      if(const char* key = std::getenv("AZURE_STORAGE_ACCOUNT"))
      {
        account = key;
        grklog.debug("Set ADLS account = %s", account.c_str());
      }
    }
    if(secret.empty())
    {
      if(const char* key = std::getenv("AZURE_STORAGE_KEY"))
      {
        secret = key;
        grklog.debug("Set ADLS key = %s", secret.c_str());
      }
      else if(const char* sas = std::getenv("AZURE_STORAGE_SAS_TOKEN"))
      {
        secret = sas;
        grklog.debug("Set ADLS SAS token = %s", secret.c_str());
      }
    }

    // Apply authentication
    if(!account.empty() && !secret.empty() && secret.find("?") != 0)
    {
      // Use account key authentication
      curl_easy_setopt(curl, CURLOPT_USERNAME, account.c_str());
      curl_easy_setopt(curl, CURLOPT_PASSWORD, secret.c_str());
      grklog.debug("Applied ADLS account key authentication for account: %s", account.c_str());
    }
    else if(!secret.empty() && secret.find("?") == 0)
    {
      // Append SAS token to URL
      final_url += secret;
      curl_easy_setopt(curl, CURLOPT_URL, final_url.c_str());
      grklog.debug("Applied ADLS SAS token authentication, final URL: %s", final_url.c_str());
    }
    else
    {
      grklog.debug("No additional ADLS authentication applied (public access or other auth)");
    }
  }

  curl_slist* prepareAuthHeaders(curl_slist* headers) override
  {
    time_t now;
    time(&now);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    std::string date_header = "x-ms-date: " + std::string(date_buf);
    headers = curl_slist_append(headers, date_header.c_str());

    return curl_slist_append(headers, "x-ms-version: 2020-04-08");
  }
};

} // namespace grk

#endif