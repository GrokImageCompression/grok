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
#include <cstdlib>
#include <ctime>
#include <string>

#ifdef GRK_ENABLE_LIBCURL

namespace grk
{

class S3Fetcher : public CurlFetcher
{
protected:
  void parse(const std::string& path) override
  {
    bool is_streaming = path.starts_with("/vsis3_streaming/");
    grklog.debug("Starting parse with path: %s (streaming: %s)", path.c_str(),
                 is_streaming ? "true" : "false");

    configureAuthFromEnv();
    bool use_virtual_hosting = isVirtualHostingEnabled();

    ParsedFetchPath parsed;
    std::string url_or_vsi = path;

    if(url_or_vsi.starts_with("/vsis3/") || is_streaming)
    {
      FetchPathParser::parseVsiPath(url_or_vsi, parsed, is_streaming ? "vsis3_streaming" : "vsis3");
      configureEndpoint(parsed, use_virtual_hosting);
    }
    else if(url_or_vsi.starts_with("https://"))
    {
      bool is_virtual_host = handleVirtualHosting(url_or_vsi, parsed);
      if(!is_virtual_host)
      {
        FetchPathParser::parseHttpsPath(url_or_vsi, parsed);
      }
    }
    else
    {
      grklog.error("Unsupported URL format: %s", url_or_vsi.c_str());
      throw std::runtime_error("Unsupported URL format");
    }

    grklog.debug("Final parsed values - Host: %s, Port: %d, Bucket: %s, Key: %s",
                 parsed.host.c_str(), parsed.port, parsed.bucket.c_str(), parsed.key.c_str());

    bool use_https = true;
    if(EnvVarManager::get("AWS_HTTPS") && EnvVarManager::get_string("AWS_HTTPS") == "NO")
    {
      use_https = false;
      grklog.debug("Using HTTP due to AWS_HTTPS=NO");
    }

    if(EnvVarManager::get("AWS_S3_ENDPOINT") && !use_virtual_hosting)
    {
      url_ = (use_https ? "https://" : "http://") + parsed.host +
             (parsed.port != (use_https ? 443 : 80) ? ":" + std::to_string(parsed.port) : "") +
             "/" + parsed.bucket + "/" + parsed.key;
      grklog.debug("Constructed path-style URL with AWS_S3_ENDPOINT: %s", url_.c_str());
    }
    else
    {
      url_ = (use_https ? "https://" : "http://") + parsed.host +
             (parsed.port != (use_https ? 443 : 80) ? ":" + std::to_string(parsed.port) : "") +
             "/" + parsed.key;
      grklog.debug("Constructed URL (virtual host or AWS default): %s", url_.c_str());
    }
  }

  void auth(CURL* curl) override
  {
    CurlFetcher::auth(curl);

    // Skip authentication for public buckets
    if(EnvVarManager::get("AWS_NO_SIGN_REQUEST") &&
       EnvVarManager::get_string("AWS_NO_SIGN_REQUEST") == "YES")
    {
      grklog.debug("Skipping SigV4 signing for AWS_NO_SIGN_REQUEST=YES");
      return;
    }

    // Apply SigV4 signing
    std::string sigv4 = "aws:amz:" + auth_.region_ + ":s3";
    curl_easy_setopt(curl, CURLOPT_AWS_SIGV4, sigv4.c_str());

    // Handle SSL verification
    if(EnvVarManager::get("CPL_VSIL_CURL_ALLOW_INSECURE") &&
       EnvVarManager::get_string("CPL_VSIL_CURL_ALLOW_INSECURE") == "YES")
    {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
      grklog.debug("Disabled SSL verification for CPL_VSIL_CURL_ALLOW_INSECURE=YES");
    }

    // Handle non-cached connections
    if(EnvVarManager::get("CPL_VSIL_CURL_NON_CACHED") &&
       EnvVarManager::get_string("CPL_VSIL_CURL_NON_CACHED") == "/vsis3/")
    {
      curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
      grklog.debug("Disabled connection reuse for CPL_VSIL_CURL_NON_CACHED");
    }

    // Set timeouts
    if(auto timeout = EnvVarManager::get("CPL_VSIL_CURL_TIMEOUT"))
    {
      try
      {
        long timeout_val = std::stol(*timeout);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_val);
        grklog.debug("Set timeout to %ld seconds", timeout_val);
      }
      catch(const std::exception& e)
      {
        grklog.warn("Invalid CPL_VSIL_CURL_TIMEOUT: %s", timeout->c_str());
      }
    }

    // Set cache size
    if(auto cache_size = EnvVarManager::get("CPL_VSIL_CURL_CACHE_SIZE"))
    {
      try
      {
        long cache_size_val = std::stol(*cache_size);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, cache_size_val);
        grklog.debug("Set cache size to %ld bytes", cache_size_val);
      }
      catch(const std::exception& e)
      {
        grklog.warn("Invalid CPL_VSIL_CURL_CACHE_SIZE: %s", cache_size->c_str());
      }
    }

    // Proxy settings
    if(auto proxy = EnvVarManager::get("CPL_VSIL_CURL_PROXY"))
    {
      curl_easy_setopt(curl, CURLOPT_PROXY, proxy->c_str());
      grklog.debug("Set proxy: %s", proxy->c_str());
      if(auto proxy_userpwd = EnvVarManager::get("CPL_VSIL_CURL_PROXYUSERPWD"))
      {
        curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxy_userpwd->c_str());
        grklog.debug("Set proxy credentials");
      }
      if(auto proxy_auth = EnvVarManager::get("CPL_VSIL_CURL_PROXYAUTH"))
      {
        curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
        grklog.debug("Set proxy authentication: %s", proxy_auth->c_str());
      }
    }
  }

  curl_slist* prepareAuthHeaders(curl_slist* headers) override
  {
    time_t now;
    time(&now);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y%m%dT%H%M%SZ", gmtime(&now));
    std::string amz_date = "x-amz-date: " + std::string(date_buf);
    headers = curl_slist_append(headers, amz_date.c_str());

    // Add x-amz-security-token if session_token_ is set
    if(!auth_.session_token_.empty())
    {
      std::string security_token = "x-amz-security-token: " + auth_.session_token_;
      headers = curl_slist_append(headers, security_token.c_str());
      grklog.debug("Added x-amz-security-token header");
    }
    else
    {
      grklog.debug("No session token provided, skipping x-amz-security-token header");
    }

    return headers;
  }

private:
  bool fetchInstanceMetadata(std::string& access_key, std::string& secret_key,
                             std::string& session_token)
  {
    CURL* curl = curl_easy_init();
    if(!curl)
    {
      grklog.error("Failed to initialize curl for instance metadata");
      return false;
    }

    std::string token_url = "http://169.254.169.254/latest/api/token";
    std::string role_url = "http://169.254.169.254/latest/meta-data/iam/security-credentials/";
    std::string response;

    // Get IMDSv2 token
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "X-aws-ec2-metadata-token-ttl-seconds: 21600");
    curl_easy_setopt(curl, CURLOPT_URL, token_url.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlFetcher::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      grklog.debug("Failed to get IMDSv2 token: %d", res);
      return false;
    }
    std::string token = response;
    response.clear();

    // Get role name
    curl_slist_free_all(headers);
    headers = nullptr;
    headers = curl_slist_append(headers, ("X-aws-ec2-metadata-token: " + token).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, role_url.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      grklog.debug("Failed to get IAM role name: %d", res);
      return false;
    }
    std::string role_name = response;
    role_name.erase(role_name.find_last_not_of(" \t\r\n") + 1);
    response.clear();

    // Get credentials
    curl_slist_free_all(headers);
    headers = nullptr;
    headers = curl_slist_append(headers, ("X-aws-ec2-metadata-token: " + token).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, (role_url + role_name).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      grklog.debug("Failed to get IAM credentials: %d", res);
      return false;
    }

    // Parse JSON response (simplified, assumes key-value pairs)
    size_t key_pos = response.find("\"AccessKeyId\":\"");
    size_t secret_pos = response.find("\"SecretAccessKey\":\"");
    size_t token_pos = response.find("\"Token\":\"");
    if(key_pos != std::string::npos && secret_pos != std::string::npos &&
       token_pos != std::string::npos)
    {
      access_key =
          response.substr(key_pos + 14, response.find("\"", key_pos + 14) - (key_pos + 14));
      secret_key = response.substr(secret_pos + 18,
                                   response.find("\"", secret_pos + 18) - (secret_pos + 18));
      session_token =
          response.substr(token_pos + 9, response.find("\"", token_pos + 9) - (token_pos + 9));
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      return true;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return false;
  }

  void configureAuthFromEnv()
  {
    // Parse config file for region
    std::string config_file;
    if(auto aws_config = EnvVarManager::get("AWS_CONFIG_FILE"))
    {
      config_file = *aws_config;
      grklog.debug("Using config file from AWS_CONFIG_FILE: %s", config_file.c_str());
    }
    else
    {
#ifdef _WIN32
      if(auto user_profile = EnvVarManager::get("USERPROFILE"))
      {
        config_file = *user_profile + "\\.aws\\config";
      }
#else
      if(auto home = EnvVarManager::get("HOME"))
      {
        config_file = *home + "/.aws/config";
      }
#endif
      grklog.debug("Using default config file: %s", config_file.c_str());
    }

    std::string profile = "default";
    if(auto profile_env = EnvVarManager::get("AWS_PROFILE"))
    {
      profile = *profile_env;
      grklog.debug("AWS_PROFILE set: %s", profile.c_str());
    }
    else if(auto default_profile = EnvVarManager::get("AWS_DEFAULT_PROFILE"))
    {
      profile = *default_profile;
      grklog.debug("AWS_DEFAULT_PROFILE set: %s", profile.c_str());
    }

    if(!config_file.empty() && auth_.region_.empty())
    {
      IniParser parser;
      if(parser.parse(config_file))
      {
        auto profile_it = parser.sections.find("profile " + profile);
        if(profile_it == parser.sections.end())
          profile_it = parser.sections.find(profile);
        if(profile_it != parser.sections.end())
        {
          if(auto region_it = profile_it->second.find("region");
             region_it != profile_it->second.end())
          {
            auth_.region_ = region_it->second;
            grklog.debug("Set region from config profile '%s': %s", profile.c_str(),
                         auth_.region_.c_str());
          }
        }
        else
        {
          grklog.debug("Profile '%s' not found in config file", profile.c_str());
        }
      }
    }

    // Prioritize AWS_REGION environment variable
    if(auto region = EnvVarManager::get("AWS_REGION"))
    {
      auth_.region_ = *region;
      grklog.debug("Set region from AWS_REGION: %s", auth_.region_.c_str());
    }
    else if(auth_.region_.empty())
    {
      auth_.region_ = "us-east-1";
      grklog.debug("Region empty, defaulting to: %s", auth_.region_.c_str());
    }

    // Check environment variables first (highest precedence)
    if(auto key = EnvVarManager::get("AWS_ACCESS_KEY_ID"))
    {
      auth_.username_ = *key;
      grklog.debug("Set access key from AWS_ACCESS_KEY_ID: %s", auth_.username_.c_str());
    }
    if(auto secret = EnvVarManager::get("AWS_SECRET_ACCESS_KEY"))
    {
      auth_.password_ = *secret;
      grklog.debug("Set secret key from AWS_SECRET_ACCESS_KEY: %s", auth_.password_.c_str());
    }
    if(auto token = EnvVarManager::get("AWS_SESSION_TOKEN"))
    {
      auth_.session_token_ = *token;
      grklog.debug("Set session token from AWS_SESSION_TOKEN: %s", auth_.session_token_.c_str());
    }

    // Validate temporary credentials
    if(!auth_.session_token_.empty() && (auth_.username_.empty() || auth_.password_.empty()))
    {
      grklog.warn("Session token provided but access key or secret key missing");
    }

    // If environment variables are not set, try AWS credentials file
    if(auth_.username_.empty() || auth_.password_.empty())
    {
      std::string credentials_file;
      if(auto cpl_file = EnvVarManager::get("CPL_AWS_CREDENTIALS_FILE"))
      {
        credentials_file = *cpl_file;
        grklog.debug("Using credentials file from CPL_AWS_CREDENTIALS_FILE: %s",
                     credentials_file.c_str());
      }
      else
      {
#ifdef _WIN32
        if(auto user_profile = EnvVarManager::get("USERPROFILE"))
        {
          credentials_file = *user_profile + "\\.aws\\credentials";
        }
#else
        if(auto home = EnvVarManager::get("HOME"))
        {
          credentials_file = *home + "/.aws/credentials";
        }
#endif
        grklog.debug("Using default credentials file: %s", credentials_file.c_str());
      }

      if(!credentials_file.empty())
      {
        IniParser parser;
        if(parser.parse(credentials_file))
        {
          auto profile_it = parser.sections.find(profile);
          if(profile_it != parser.sections.end())
          {
            grklog.debug("Found profile '%s' in credentials file", profile.c_str());
            if(auth_.username_.empty())
            {
              auto key_it = profile_it->second.find("aws_access_key_id");
              if(key_it != profile_it->second.end())
              {
                auth_.username_ = key_it->second;
                grklog.debug("Set access key from profile '%s': %s", profile.c_str(),
                             auth_.username_.c_str());
              }
            }
            if(auth_.password_.empty())
            {
              auto secret_it = profile_it->second.find("aws_secret_access_key");
              if(secret_it != profile_it->second.end())
              {
                auth_.password_ = secret_it->second;
                grklog.debug("Set secret key from profile '%s': %s", profile.c_str(),
                             auth_.password_.c_str());
              }
            }
            if(auth_.session_token_.empty())
            {
              auto token_it = profile_it->second.find("aws_session_token");
              if(token_it != profile_it->second.end())
              {
                auth_.session_token_ = token_it->second;
                grklog.debug("Set session token from profile '%s': %s", profile.c_str(),
                             auth_.session_token_.c_str());
              }
            }
          }
          else
          {
            grklog.debug("Profile '%s' not found in credentials file", profile.c_str());
          }
        }
      }
    }

    // Try instance metadata if credentials are still missing
    if(auth_.username_.empty() || auth_.password_.empty())
    {
      std::string access_key, secret_key, session_token;
      if(fetchInstanceMetadata(access_key, secret_key, session_token))
      {
        auth_.username_ = access_key;
        auth_.password_ = secret_key;
        auth_.session_token_ = session_token;
        grklog.debug("Set credentials from EC2 instance metadata: access_key=%s",
                     auth_.username_.c_str());
      }
      else
      {
        grklog.debug("Failed to retrieve credentials from EC2 instance metadata");
      }
    }

    // Fallback to struct-provided credentials if still empty
    if(auth_.username_.empty() && !auth_.username_.empty())
    {
      grklog.debug("Using struct-provided access key: %s", auth_.username_.c_str());
    }
    if(auth_.password_.empty() && !auth_.password_.empty())
    {
      grklog.debug("Using struct-provided secret key: %s", auth_.password_.c_str());
    }
    if(auth_.session_token_.empty() && !auth_.session_token_.empty())
    {
      grklog.debug("Using struct-provided session token: %s", auth_.session_token_.c_str());
    }

    // Log final credential state
    if(auth_.username_.empty())
      grklog.debug("No access key provided via env, profile, metadata, or struct");
    if(auth_.password_.empty())
      grklog.debug("No secret key provided via env, profile, metadata, or struct");
    if(auth_.session_token_.empty())
      grklog.debug("No session token provided via env, profile, metadata, or struct");
  }

  bool isVirtualHostingEnabled()
  {
    if(auto virtual_hosting = EnvVarManager::get("AWS_VIRTUAL_HOSTING"))
    {
      std::string vh = *virtual_hosting;
      bool enabled = (vh == "TRUE" || vh == "true" || vh == "1");
      grklog.debug("AWS_VIRTUAL_HOSTING set to: %s (use_virtual_hosting: %s)", vh.c_str(),
                   enabled ? "true" : "false");
      return enabled;
    }
    grklog.debug("AWS_VIRTUAL_HOSTING not set, defaulting to false");
    return false;
  }

  void configureEndpoint(ParsedFetchPath& parsed, bool use_virtual_hosting)
  {
    if(auto s3_endpoint = EnvVarManager::get("AWS_S3_ENDPOINT"))
    {
      std::string endpoint = *s3_endpoint;
      grklog.debug("AWS_S3_ENDPOINT set: %s", endpoint.c_str());

      if(endpoint.starts_with("https://"))
      {
        endpoint = endpoint.substr(8);
        grklog.debug("Stripped https:// from endpoint: %s", endpoint.c_str());
      }
      else if(endpoint.starts_with("http://"))
      {
        endpoint = endpoint.substr(7);
        grklog.debug("Stripped http:// from endpoint: %s", endpoint.c_str());
      }

      size_t port_pos = endpoint.find(':');
      if(port_pos != std::string::npos)
      {
        parsed.host = endpoint.substr(0, port_pos);
        std::string port_str = endpoint.substr(port_pos + 1);
        grklog.debug("Endpoint split: host=%s, port_str=%s", parsed.host.c_str(), port_str.c_str());
        try
        {
          parsed.port = port_str.empty() ? (EnvVarManager::get("AWS_HTTPS") &&
                                                    EnvVarManager::get_string("AWS_HTTPS") == "NO"
                                                ? 80
                                                : 443)
                                         : std::stoi(port_str);
          grklog.debug("Parsed port: %d", parsed.port);
        }
        catch(const std::exception& e)
        {
          parsed.port =
              (EnvVarManager::get("AWS_HTTPS") && EnvVarManager::get_string("AWS_HTTPS") == "NO")
                  ? 80
                  : 443;
          grklog.error("Invalid port in AWS_S3_ENDPOINT: %s, using default %d", port_str.c_str(),
                       parsed.port);
        }
      }
      else
      {
        parsed.host = endpoint;
        parsed.port =
            (EnvVarManager::get("AWS_HTTPS") && EnvVarManager::get_string("AWS_HTTPS") == "NO")
                ? 80
                : 443;
        grklog.debug("No port in endpoint, using host=%s, port=%d", parsed.host.c_str(),
                     parsed.port);
      }

      if(use_virtual_hosting)
      {
        parsed.host = parsed.bucket + "." + parsed.host;
        grklog.debug("Applied virtual host-style: host=%s", parsed.host.c_str());
      }
    }
    else
    {
      parsed.host = use_virtual_hosting ? parsed.bucket + ".s3." + auth_.region_ + ".amazonaws.com"
                                        : "s3." + auth_.region_ + ".amazonaws.com";
      parsed.port =
          (EnvVarManager::get("AWS_HTTPS") && EnvVarManager::get_string("AWS_HTTPS") == "NO") ? 80
                                                                                              : 443;
      grklog.debug("AWS_S3_ENDPOINT unset, using AWS S3: host=%s, port=%d", parsed.host.c_str(),
                   parsed.port);
    }
  }

  bool handleVirtualHosting(const std::string& url, ParsedFetchPath& parsed)
  {
    if(!isVirtualHostingEnabled())
    {
      grklog.debug("Virtual hosting disabled, skipping virtual host check");
      return false;
    }

    std::string url_copy = url;
    if(url_copy.starts_with("https://"))
    {
      url_copy = url_copy.substr(8);
    }
    else if(url_copy.starts_with("http://"))
    {
      url_copy = url_copy.substr(7);
    }

    FetchPathParser::parseHttpsPath(url_copy, parsed);

    std::string s3_endpoint = EnvVarManager::get("AWS_S3_ENDPOINT")
                                  ? *EnvVarManager::get("AWS_S3_ENDPOINT")
                                  : "s3." + auth_.region_ + ".amazonaws.com";
    grklog.debug("Using s3_endpoint: %s", s3_endpoint.c_str());

    if(s3_endpoint.starts_with("https://"))
    {
      s3_endpoint = s3_endpoint.substr(8);
    }
    else if(s3_endpoint.starts_with("http://"))
    {
      s3_endpoint = s3_endpoint.substr(7);
    }

    size_t port_pos = s3_endpoint.find(':');
    if(port_pos != std::string::npos)
    {
      s3_endpoint = s3_endpoint.substr(0, port_pos);
    }

    if(parsed.host.ends_with(s3_endpoint))
    {
      grklog.debug("Host ends with s3_endpoint, checking for virtual host");
      size_t bucket_len = parsed.host.length() - s3_endpoint.length() - 1;
      if(bucket_len > 0 && parsed.host[bucket_len] == '.')
      {
        parsed.bucket = parsed.host.substr(0, bucket_len);
        parsed.key = url_copy.substr(url_copy.find('/') + 1);
        grklog.debug("Detected virtual host-style URL: bucket=%s, key=%s", parsed.bucket.c_str(),
                     parsed.key.c_str());
        return true;
      }
    }
    grklog.debug("Host does not match s3_endpoint, not a virtual host");
    return false;
  }
};

} // namespace grk

#endif