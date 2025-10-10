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
#include <stdexcept>

namespace grk
{

class FetchPathParser
{
public:
  static void parseVsiPath(std::string& url, ParsedFetchPath& parsed, const std::string& prefix)
  {
    std::string full_prefix = "/" + prefix + "/";
    grklog.debug("Processing VSI path with prefix '%s': %s", prefix.c_str(), url.c_str());
    if(!url.starts_with(full_prefix))
    {
      grklog.error("Invalid VSI path, does not start with %s: %s", full_prefix.c_str(),
                   url.c_str());
      throw std::runtime_error("Invalid VSI path: does not start with " + full_prefix);
    }
    url = url.substr(full_prefix.length());
    grklog.debug("Stripped %s prefix, remaining: %s", full_prefix.c_str(), url.c_str());

    parseBucketKey(url, parsed, prefix.c_str());
  }

  // Parse HTTPS path (generic host/port and bucket/key extraction)
  static void parseHttpsPath(std::string& url, ParsedFetchPath& parsed, int default_port = 443)
  {
    grklog.debug("Processing HTTPS path: %s", url.c_str());
    if(!url.starts_with("https://"))
    {
      grklog.error("Invalid HTTPS URL, does not start with https://: %s", url.c_str());
      throw std::runtime_error("Invalid HTTPS URL: does not start with https://");
    }
    url = url.substr(8);
    grklog.debug("Stripped https:// prefix, remaining: %s", url.c_str());

    // Parse host and port
    parseHostPort(url, parsed, default_port);

    // Extract path and parse bucket/key
    std::string path = url.substr(url.find('/') + 1);
    grklog.debug("Extracted path: %s", path.c_str());
    parseBucketKey(path, parsed, "HTTPS");
  }

private:
  // Parse host and port from URL
  static void parseHostPort(const std::string& url, ParsedFetchPath& parsed, int default_port)
  {
    size_t host_end = url.find(':');
    std::string port_str;
    if(host_end == std::string::npos)
    {
      host_end = url.find('/');
      if(host_end == std::string::npos)
      {
        grklog.error("Invalid HTTPS URL: no bucket/key separator in: %s", url.c_str());
        throw std::runtime_error("Invalid HTTPS URL: no bucket/key separator");
      }
      parsed.host = url.substr(0, host_end);
      parsed.port = default_port;
      grklog.debug("No port specified, using host=%s, port=%d", parsed.host.c_str(), parsed.port);
    }
    else
    {
      parsed.host = url.substr(0, host_end);
      port_str = url.substr(host_end + 1, url.find('/') - host_end - 1);
      grklog.debug("Port specified: host=%s, port_str=%s", parsed.host.c_str(), port_str.c_str());
      try
      {
        parsed.port = port_str.empty() ? default_port : std::stoi(port_str);
        grklog.debug("Parsed port: %d", parsed.port);
      }
      catch(const std::exception& e)
      {
        grklog.error("Invalid port in HTTPS URL: %s, using default %d", port_str.c_str(),
                     default_port);
        parsed.port = default_port;
      }
    }
  }

  // Parse bucket and key from path
  static void parseBucketKey(const std::string& path, ParsedFetchPath& parsed,
                             const char* log_context)
  {
    size_t bucket_end = path.find('/');
    if(bucket_end == std::string::npos)
    {
      grklog.error("Invalid %s URL: no key after bucket in path: %s", log_context, path.c_str());
      throw std::runtime_error(std::string("Invalid ") + log_context + " URL: no key after bucket");
    }
    parsed.bucket = path.substr(0, bucket_end);
    parsed.key = path.substr(bucket_end + 1);
    grklog.debug("%s parsed: bucket=%s, key=%s", log_context, parsed.bucket.c_str(),
                 parsed.key.c_str());
  }
};

} // namespace grk