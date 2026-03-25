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

#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <string>
#include <mutex>
#include <fstream>
#include <filesystem>

#include "grk_config_private.h"
#include "CurlFetcher.h"
#include "FetchPathParser.h"
#include "IniParser.h"

#ifdef GRK_ENABLE_LIBCURL

namespace grk
{

/**
 * S3Fetcher — AWS S3 and S3-compatible object storage fetcher.
 *
 * Supports AWS S3, MinIO, DigitalOcean Spaces, Backblaze B2, Cloudflare R2,
 * and any other S3-compatible endpoint. All environment variables are compatible
 * with GDAL's /vsis3/ virtual filesystem.
 *
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  URL FORMATS                                                            ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  /vsis3/bucket/key              VSI path                                ║
 * ║  /vsis3_streaming/bucket/key    VSI streaming path                      ║
 * ║  https://host/bucket/key        Direct HTTPS URL                        ║
 * ║  http://host:port/bucket/key    Direct HTTP URL (e.g. MinIO)            ║
 * ║  https://bucket.s3.region.amazonaws.com/key   Virtual-hosted URL        ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 *
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  CREDENTIAL CHAIN (in precedence order, first match wins)               ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                         ║
 * ║  1. Anonymous access                                                    ║
 * ║     AWS_NO_SIGN_REQUEST=YES        Skip all authentication              ║
 * ║                                                                         ║
 * ║  2. Environment variables                                               ║
 * ║     AWS_ACCESS_KEY_ID              Access key                           ║
 * ║     AWS_SECRET_ACCESS_KEY          Secret key                           ║
 * ║     AWS_SESSION_TOKEN              Temporary session token (optional)    ║
 * ║                                                                         ║
 * ║  3. Cached temporary credentials                                        ║
 * ║     In-memory cache with expiration tracking (60s safety margin).       ║
 * ║     Thread-safe, shared across all S3Fetcher instances.                 ║
 * ║                                                                         ║
 * ║  4. AWS config files (~/.aws/credentials, ~/.aws/config)                ║
 * ║     GRK_AWS_CREDENTIALS_FILE       Override credentials file path       ║
 * ║     AWS_CONFIG_FILE                Override config file path            ║
 * ║     AWS_PROFILE                    Profile name (default: "default")    ║
 * ║     AWS_DEFAULT_PROFILE            Deprecated alias for AWS_PROFILE     ║
 * ║                                                                         ║
 * ║     Config file supports these advanced credential sources:             ║
 * ║                                                                         ║
 * ║     4a. Web Identity Token via config                                   ║
 * ║         [profile X]                                                     ║
 * ║         role_arn = arn:aws:iam::...:role/...                            ║
 * ║         web_identity_token_file = /path/to/token                        ║
 * ║                                                                         ║
 * ║     4b. STS Assume Role                                                 ║
 * ║         [profile X]                                                     ║
 * ║         role_arn = arn:aws:iam::...:role/...                            ║
 * ║         source_profile = base-profile                                   ║
 * ║         external_id = optional                                          ║
 * ║         role_session_name = optional                                    ║
 * ║         Supports chaining: source_profile may itself use web identity.  ║
 * ║                                                                         ║
 * ║     4c. SSO (IAM Identity Center)                                       ║
 * ║         [profile X]                                                     ║
 * ║         sso_start_url = https://org.awsapps.com/start                   ║
 * ║         sso_account_id = 123456789012                                   ║
 * ║         sso_role_name = MyRole                                          ║
 * ║         sso_session = optional-session-name                             ║
 * ║         Reads cached tokens from ~/.aws/sso/cache/.                     ║
 * ║         GRK_AWS_SSO_ENDPOINT       Override SSO portal endpoint         ║
 * ║                                                                         ║
 * ║     4d. Credential process                                              ║
 * ║         [profile X]                                                     ║
 * ║         credential_process = /path/to/provider --arg                    ║
 * ║         Must output JSON v1 with AccessKeyId, SecretAccessKey,          ║
 * ║         SessionToken, and optionally Expiration.                        ║
 * ║                                                                         ║
 * ║  5. Web Identity Token from environment (EKS/IRSA/OIDC)                ║
 * ║     AWS_ROLE_ARN                   Role ARN to assume                   ║
 * ║     AWS_WEB_IDENTITY_TOKEN_FILE    Path to OIDC token file              ║
 * ║     AWS_ROLE_SESSION_NAME          Session name (default: grok-session) ║
 * ║     GRK_AWS_WEB_IDENTITY_ENABLE    YES (default) / NO                   ║
 * ║                                                                         ║
 * ║  6. ECS container credentials                                           ║
 * ║     AWS_CONTAINER_CREDENTIALS_FULL_URI       Full endpoint URL          ║
 * ║     AWS_CONTAINER_CREDENTIALS_RELATIVE_URI   Relative path (→ 170.2)   ║
 * ║     AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE   Auth token file path       ║
 * ║     AWS_CONTAINER_AUTHORIZATION_TOKEN        Auth token value           ║
 * ║                                                                         ║
 * ║  7. EC2 instance metadata (IMDSv2 → IMDSv1 fallback)                   ║
 * ║     GRK_AWS_EC2_API_ROOT_URL       Override (default: 169.254.169.254)  ║
 * ║     GRK_AWS_AUTODETECT_EC2_DISABLE Set YES to skip EC2 detection        ║
 * ║                                                                         ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  REGION CONFIGURATION  (resolved in order)                              ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  AWS_REGION                        Highest precedence                   ║
 * ║  AWS_DEFAULT_REGION                Standard AWS SDK variable            ║
 * ║  Config file region field          From [profile X] in ~/.aws/config    ║
 * ║  (fallback)                        us-east-1                            ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  ENDPOINT CONFIGURATION                                                 ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  AWS_S3_ENDPOINT          Custom endpoint (e.g. http://localhost:9000)   ║
 * ║  AWS_HTTPS                YES (default) / NO — protocol selection       ║
 * ║  AWS_VIRTUAL_HOSTING      TRUE / FALSE (default) — URL style            ║
 * ║                                                                         ║
 * ║  MinIO example:                                                         ║
 * ║    AWS_S3_ENDPOINT=http://localhost:9000                                 ║
 * ║    AWS_ACCESS_KEY_ID=minioadmin                                         ║
 * ║    AWS_SECRET_ACCESS_KEY=minioadmin                                     ║
 * ║    AWS_HTTPS=NO                                                         ║
 * ║                                                                         ║
 * ║  STS endpoint:                                                          ║
 * ║  AWS_STS_REGIONAL_ENDPOINTS   regional (default) / legacy               ║
 * ║  GRK_AWS_STS_ROOT_URL         Override STS endpoint entirely            ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  REQUESTER PAYS                                                         ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  AWS_REQUEST_PAYER=requester   Adds x-amz-request-payer header          ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  HTTP / CURL CONFIGURATION                                              ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  GRK_CURL_ALLOW_INSECURE           YES / NO — disable SSL verification  ║
 * ║  GRK_HTTP_UNSAFESSL                YES / NO — same (from CurlFetcher)   ║
 * ║  GRK_CURL_TIMEOUT                  Request timeout in seconds            ║
 * ║  GRK_CURL_CACHE_SIZE               Curl receive buffer size in bytes     ║
 * ║  GRK_CURL_NON_CACHED               Disable connection reuse for prefix   ║
 * ║  GRK_CURL_PROXY                    Proxy URL                            ║
 * ║  GRK_CURL_PROXYUSERPWD             Proxy user:password                  ║
 * ║  GRK_CURL_PROXYAUTH                Enable proxy auth (CURLAUTH_ANY)     ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  FILE PATHS                                                              ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  GRK_AWS_ROOT_DIR                  Override ~/.aws root directory        ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 *
 * Request signing uses AWS Signature V4 via libcurl's CURLOPT_AWS_SIGV4.
 */

// Credential source tracking (mirrors GDAL's AWSCredentialsSource)
enum class AWSCredentialSource
{
  NONE,
  NO_SIGN_REQUEST,
  ENVIRONMENT,
  CONFIG_FILE,
  WEB_IDENTITY,
  ASSUMED_ROLE,
  SSO,
  CREDENTIAL_PROCESS,
  EC2_OR_ECS
};

class S3Fetcher : public CurlFetcher
{
  // Cached credentials shared across all S3Fetcher instances.
  // Temporary credentials (STS, SSO, EC2, etc.) are cached until expiration
  // with a 60-second safety margin, matching GDAL behavior.
  struct CredentialCache
  {
    std::mutex mutex;
    AWSCredentialSource source = AWSCredentialSource::NONE;
    std::string accessKey;
    std::string secretKey;
    std::string sessionToken;
    std::string region;
    time_t expiration = 0;

    // State for credential refresh
    std::string roleArn;
    std::string webIdentityTokenFile;
    std::string ssoStartURL;
    std::string ssoAccountId;
    std::string ssoRoleName;
    std::string credentialProcess;
    std::string sourceAccessKey;
    std::string sourceSecretKey;
    std::string sourceSessionToken;
    std::string externalId;
    std::string roleSessionName;

    bool isValid() const
    {
      if(accessKey.empty() || secretKey.empty())
        return false;
      if(expiration > 0)
      {
        time_t now;
        time(&now);
        return now < expiration - 60;
      }
      return true;
    }
  };

  static CredentialCache& cache()
  {
    static CredentialCache instance;
    return instance;
  }

protected:
  void parse(const std::string& path) override
  {
    bool is_streaming = path.starts_with("/vsis3_streaming/");
    grklog.debug("S3Fetcher: parsing path: %s (streaming: %s)", path.c_str(),
                 is_streaming ? "true" : "false");

    configureAuth();
    bool use_virtual_hosting = isVirtualHostingEnabled();

    ParsedFetchPath parsed;
    std::string url_or_vsi = path;

    if(url_or_vsi.starts_with("/vsis3/") || is_streaming)
    {
      FetchPathParser::parseVsiPath(url_or_vsi, parsed, is_streaming ? "vsis3_streaming" : "vsis3");
      configureEndpoint(parsed, use_virtual_hosting);
    }
    else if(url_or_vsi.starts_with("https://") || url_or_vsi.starts_with("http://"))
    {
      bool is_virtual_host = handleVirtualHosting(url_or_vsi, parsed);
      if(!is_virtual_host)
      {
        if(url_or_vsi.starts_with("https://"))
          FetchPathParser::parseHttpsPath(url_or_vsi, parsed);
        else
          parseHttpUrl(url_or_vsi, parsed);
      }
    }
    else
    {
      grklog.error("Unsupported URL format: %s", url_or_vsi.c_str());
      throw std::runtime_error("Unsupported URL format");
    }

    grklog.debug("S3Fetcher: parsed - Host: %s, Port: %d, Bucket: %s, Key: %s", parsed.host.c_str(),
                 parsed.port, parsed.bucket.c_str(), parsed.key.c_str());

    bool use_https = useHttps();

    if(EnvVarManager::get("AWS_S3_ENDPOINT") && !use_virtual_hosting)
    {
      url_ = (use_https ? "https://" : "http://") + parsed.host +
             formatPort(parsed.port, use_https) + "/" + parsed.bucket + "/" + parsed.key;
    }
    else
    {
      url_ = (use_https ? "https://" : "http://") + parsed.host +
             formatPort(parsed.port, use_https) + "/" + parsed.key;
    }
    grklog.debug("S3Fetcher: constructed URL: %s", url_.c_str());
  }

  void auth(CURL* curl) override
  {
    CurlFetcher::auth(curl);

    if(noSignRequest_)
    {
      grklog.debug("S3Fetcher: skipping SigV4 signing (AWS_NO_SIGN_REQUEST)");
      return;
    }

    // SigV4 signing via libcurl
    std::string sigv4 = "aws:amz:" + auth_.region_ + ":s3";
    curl_easy_setopt(curl, CURLOPT_AWS_SIGV4, sigv4.c_str());

    // SSL verification
    if(EnvVarManager::test_bool("GRK_CURL_ALLOW_INSECURE"))
    {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    // Connection reuse
    auto nonCached = EnvVarManager::get("GRK_CURL_NON_CACHED");
    if(nonCached && nonCached->find("/vsis3/") != std::string::npos)
    {
      curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    }

    // Timeout
    long timeout = EnvVarManager::get_int("GRK_CURL_TIMEOUT", 0);
    if(timeout > 0)
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

    // Buffer size
    long bufSize = EnvVarManager::get_int("GRK_CURL_CACHE_SIZE", 0);
    if(bufSize > 0)
      curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, bufSize);

    // Proxy
    if(auto proxy = EnvVarManager::get("GRK_CURL_PROXY"))
    {
      curl_easy_setopt(curl, CURLOPT_PROXY, proxy->c_str());
      if(auto proxyUserPwd = EnvVarManager::get("GRK_CURL_PROXYUSERPWD"))
        curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxyUserPwd->c_str());
      if(EnvVarManager::get("GRK_CURL_PROXYAUTH"))
        curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    }
  }

  curl_slist* prepareAuthHeaders(curl_slist* headers) override
  {
    if(noSignRequest_)
      return headers;

    time_t now;
    time(&now);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y%m%dT%H%M%SZ", gmtime(&now));
    headers = curl_slist_append(headers, ("x-amz-date: " + std::string(date_buf)).c_str());

    if(!auth_.session_token_.empty())
    {
      headers =
          curl_slist_append(headers, ("x-amz-security-token: " + auth_.session_token_).c_str());
    }

    if(!requestPayer_.empty())
    {
      headers = curl_slist_append(headers, ("x-amz-request-payer: " + requestPayer_).c_str());
    }

    return headers;
  }

private:
  bool noSignRequest_ = false;
  std::string requestPayer_;

  // ═══════════════════════════════════════════════════════════════════
  //  Credential chain (matches GDAL precedence order)
  // ═══════════════════════════════════════════════════════════════════

  void configureAuth()
  {
    // Requester pays
    requestPayer_ = EnvVarManager::get_string("AWS_REQUEST_PAYER");

    // AWS_NO_SIGN_REQUEST — anonymous access for public buckets
    if(auth_.s3_no_sign_request_ || EnvVarManager::test_bool("AWS_NO_SIGN_REQUEST"))
    {
      noSignRequest_ = true;
      resolveRegion("default");
      grklog.debug("S3Fetcher: unsigned requests (AWS_NO_SIGN_REQUEST)");
      return;
    }

    // 0. Pre-configured credentials (e.g. passed by GDAL after resolving its own auth chain)
    if(!auth_.username_.empty() && !auth_.password_.empty())
    {
      if(auth_.region_.empty())
        resolveRegion("default");
      grklog.debug("S3Fetcher: credentials from pre-configured auth");
      return;
    }

    // 1. Environment variables (highest precedence)
    if(tryEnvCredentials())
    {
      grklog.debug("S3Fetcher: credentials from environment variables");
      return;
    }

    // 2. Previously cached temporary credentials
    if(tryCachedCredentials())
    {
      grklog.debug("S3Fetcher: credentials from cache");
      return;
    }

    // 3. AWS config/credentials files
    std::string profile = getProfile();
    if(tryConfigFileCredentials(profile))
    {
      grklog.debug("S3Fetcher: credentials from config files");
      return;
    }

    // 4. Web Identity Token from env vars (EKS/IRSA)
    if(EnvVarManager::test_bool("GRK_AWS_WEB_IDENTITY_ENABLE", true))
    {
      if(tryWebIdentityToken())
      {
        grklog.debug("S3Fetcher: credentials from Web Identity Token");
        return;
      }
    }

    // 5. ECS container credentials
    if(tryECSCredentials())
    {
      grklog.debug("S3Fetcher: credentials from ECS container");
      return;
    }

    // 6. EC2 instance metadata (IMDSv2 with v1 fallback)
    if(!EnvVarManager::test_bool("GRK_AWS_AUTODETECT_EC2_DISABLE"))
    {
      if(tryEC2InstanceMetadata())
      {
        grklog.debug("S3Fetcher: credentials from EC2 instance metadata");
        return;
      }
    }

    grklog.warn("S3Fetcher: no valid AWS credentials found. "
                "Set AWS_SECRET_ACCESS_KEY/AWS_ACCESS_KEY_ID, configure ~/.aws/credentials, "
                "or set AWS_NO_SIGN_REQUEST=YES for public buckets.");
  }

  // ─── 1. Environment variables ─────────────────────────────────────

  bool tryEnvCredentials()
  {
    auto secretKey = EnvVarManager::get("AWS_SECRET_ACCESS_KEY");
    if(!secretKey || secretKey->empty())
      return false;

    auto accessKey = EnvVarManager::get("AWS_ACCESS_KEY_ID");
    if(!accessKey || accessKey->empty())
    {
      grklog.warn("S3Fetcher: AWS_SECRET_ACCESS_KEY set but AWS_ACCESS_KEY_ID missing");
      return false;
    }

    auth_.username_ = *accessKey;
    auth_.password_ = *secretKey;
    auth_.session_token_ = EnvVarManager::get_string("AWS_SESSION_TOKEN");
    resolveRegion("default");
    return true;
  }

  // ─── 2. Cached temporary credentials ──────────────────────────────

  bool tryCachedCredentials()
  {
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mutex);

    if(c.source == AWSCredentialSource::NONE || !c.isValid())
      return false;

    auth_.username_ = c.accessKey;
    auth_.password_ = c.secretKey;
    auth_.session_token_ = c.sessionToken;
    if(!c.region.empty())
      auth_.region_ = c.region;
    else
      resolveRegion(getProfile());
    return true;
  }

  // ─── 3. AWS config/credentials files ──────────────────────────────

  bool tryConfigFileCredentials(const std::string& profile)
  {
    resolveRegion(profile);

    // Read ~/.aws/credentials
    std::string credFile = getCredentialsFilePath();
    if(!credFile.empty())
    {
      IniParser parser;
      if(parser.parse(credFile))
      {
        auto it = parser.sections.find(profile);
        if(it != parser.sections.end())
        {
          auto& section = it->second;
          auto keyIt = section.find("aws_access_key_id");
          auto secretIt = section.find("aws_secret_access_key");
          if(keyIt != section.end() && secretIt != section.end() && !keyIt->second.empty() &&
             !secretIt->second.empty())
          {
            auth_.username_ = keyIt->second;
            auth_.password_ = secretIt->second;
            auto tokenIt = section.find("aws_session_token");
            if(tokenIt != section.end())
              auth_.session_token_ = tokenIt->second;
            grklog.debug("S3Fetcher: found credentials in profile '%s'", profile.c_str());
            return true;
          }
        }
      }
    }

    // Read ~/.aws/config for advanced credential sources
    std::string configFile = getConfigFilePath();
    if(configFile.empty())
      return false;

    IniParser parser;
    if(!parser.parse(configFile))
      return false;

    // Find the profile section
    auto it = parser.sections.find("profile " + profile);
    if(it == parser.sections.end())
      it = parser.sections.find(profile);
    if(it == parser.sections.end())
      return false;

    auto& section = it->second;

    // ── role_arn → STS AssumeRole or WebIdentity ──
    auto roleArnIt = section.find("role_arn");
    if(roleArnIt != section.end() && !roleArnIt->second.empty())
    {
      std::string roleArn = roleArnIt->second;

      // Web Identity via config (role_arn + web_identity_token_file)
      auto tokenFileIt = section.find("web_identity_token_file");
      if(tokenFileIt != section.end() && !tokenFileIt->second.empty())
      {
        if(tryWebIdentityToken(roleArn, tokenFileIt->second))
          return true;
      }

      // Assume Role via source_profile
      auto sourceProfileIt = section.find("source_profile");
      if(sourceProfileIt != section.end() && !sourceProfileIt->second.empty())
      {
        std::string externalId;
        auto extIt = section.find("external_id");
        if(extIt != section.end())
          externalId = extIt->second;

        std::string sessionName =
            EnvVarManager::get_string("AWS_ROLE_SESSION_NAME", "grok-session");
        auto sessIt = section.find("role_session_name");
        if(sessIt != section.end())
          sessionName = sessIt->second;

        // Check if source_profile uses web identity
        auto spIt = parser.sections.find("profile " + sourceProfileIt->second);
        if(spIt == parser.sections.end())
          spIt = parser.sections.find(sourceProfileIt->second);
        if(spIt != parser.sections.end())
        {
          auto spTokenIt = spIt->second.find("web_identity_token_file");
          auto spRoleIt = spIt->second.find("role_arn");
          if(spTokenIt != spIt->second.end() && spRoleIt != spIt->second.end() &&
             !spTokenIt->second.empty() && !spRoleIt->second.empty())
          {
            if(tryWebIdentityToken(spRoleIt->second, spTokenIt->second))
            {
              if(trySTSAssumeRole(roleArn, externalId, sessionName))
                return true;
            }
          }
        }

        // Read source profile credentials directly
        if(readProfileCredentials(sourceProfileIt->second))
        {
          if(trySTSAssumeRole(roleArn, externalId, sessionName))
            return true;
        }
      }
    }

    // ── SSO ──
    auto ssoStartIt = section.find("sso_start_url");
    auto ssoSessionIt = section.find("sso_session");
    if((ssoStartIt != section.end() && !ssoStartIt->second.empty()) ||
       (ssoSessionIt != section.end() && !ssoSessionIt->second.empty()))
    {
      std::string startUrl = ssoStartIt != section.end() ? ssoStartIt->second : "";
      std::string ssoSession = ssoSessionIt != section.end() ? ssoSessionIt->second : "";

      std::string accountId, roleName;
      auto accIt = section.find("sso_account_id");
      if(accIt != section.end())
        accountId = accIt->second;
      auto roleIt = section.find("sso_role_name");
      if(roleIt != section.end())
        roleName = roleIt->second;

      // Resolve sso_session → start URL from [sso-session X] section
      if(!ssoSession.empty() && startUrl.empty())
      {
        auto sessSecIt = parser.sections.find("sso-session " + ssoSession);
        if(sessSecIt != parser.sections.end())
        {
          auto urlIt = sessSecIt->second.find("sso_start_url");
          if(urlIt != sessSecIt->second.end())
            startUrl = urlIt->second;
        }
      }

      if(trySSOCredentials(startUrl, ssoSession, accountId, roleName))
        return true;
    }

    // ── credential_process ──
    auto credProcIt = section.find("credential_process");
    if(credProcIt != section.end() && !credProcIt->second.empty())
    {
      if(tryCredentialProcess(credProcIt->second))
        return true;
    }

    return false;
  }

  bool readProfileCredentials(const std::string& profile)
  {
    std::string credFile = getCredentialsFilePath();
    if(credFile.empty())
      return false;

    IniParser parser;
    if(!parser.parse(credFile))
      return false;

    auto it = parser.sections.find(profile);
    if(it == parser.sections.end())
      return false;

    auto keyIt = it->second.find("aws_access_key_id");
    auto secretIt = it->second.find("aws_secret_access_key");
    if(keyIt == it->second.end() || secretIt == it->second.end() || keyIt->second.empty() ||
       secretIt->second.empty())
      return false;

    auth_.username_ = keyIt->second;
    auth_.password_ = secretIt->second;
    auto tokenIt = it->second.find("aws_session_token");
    if(tokenIt != it->second.end())
      auth_.session_token_ = tokenIt->second;
    return true;
  }

  // ─── 4. Web Identity Token (IRSA/OIDC/EKS) ───────────────────────

  bool tryWebIdentityToken(const std::string& roleArnIn = "", const std::string& tokenFileIn = "")
  {
    std::string roleArn =
        !roleArnIn.empty() ? roleArnIn : EnvVarManager::get_string("AWS_ROLE_ARN");
    if(roleArn.empty())
      return false;

    std::string tokenFile = !tokenFileIn.empty()
                                ? tokenFileIn
                                : EnvVarManager::get_string("AWS_WEB_IDENTITY_TOKEN_FILE");
    if(tokenFile.empty())
      return false;

    std::string token;
    if(!readFileContents(tokenFile, token) || token.empty())
    {
      grklog.warn("S3Fetcher: cannot read web identity token file: %s", tokenFile.c_str());
      return false;
    }
    trimWhitespace(token);

    // STS endpoint
    std::string stsUrl = buildSTSUrl();
    std::string sessionName = EnvVarManager::get_string("AWS_ROLE_SESSION_NAME", "grok-session");

    std::string requestUrl = stsUrl +
                             "/?Action=AssumeRoleWithWebIdentity"
                             "&RoleSessionName=" +
                             urlEncode(sessionName) +
                             "&Version=2011-06-15"
                             "&RoleArn=" +
                             urlEncode(roleArn) + "&WebIdentityToken=" + urlEncode(token);

    std::string response = curlGet(requestUrl);
    if(response.empty())
    {
      grklog.warn("S3Fetcher: STS AssumeRoleWithWebIdentity request failed");
      return false;
    }

    std::string accessKey = extractXmlValue(response, "AccessKeyId");
    std::string secretKey = extractXmlValue(response, "SecretAccessKey");
    std::string sessionToken = extractXmlValue(response, "SessionToken");
    std::string expiration = extractXmlValue(response, "Expiration");

    if(accessKey.empty() || secretKey.empty() || sessionToken.empty())
    {
      grklog.warn("S3Fetcher: STS AssumeRoleWithWebIdentity returned incomplete credentials");
      return false;
    }

    auth_.username_ = accessKey;
    auth_.password_ = secretKey;
    auth_.session_token_ = sessionToken;
    resolveRegion(getProfile());

    cacheCredentials(AWSCredentialSource::WEB_IDENTITY, accessKey, secretKey, sessionToken,
                     parseIso8601(expiration));
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    c.roleArn = roleArn;
    c.webIdentityTokenFile = tokenFile;

    grklog.debug("S3Fetcher: cached web identity credentials until %s", expiration.c_str());
    return true;
  }

  // ─── STS AssumeRole ───────────────────────────────────────────────

  bool trySTSAssumeRole(const std::string& roleArn, const std::string& externalId,
                        const std::string& sessionName)
  {
    if(roleArn.empty() || auth_.username_.empty() || auth_.password_.empty())
    {
      grklog.warn("S3Fetcher: cannot assume role without source credentials");
      return false;
    }

    std::string sourceAccessKey = auth_.username_;
    std::string sourceSecretKey = auth_.password_;
    std::string sourceSessionToken = auth_.session_token_;

    std::string region = auth_.region_.empty() ? "us-east-1" : auth_.region_;
    std::string stsUrl = buildSTSUrl();

    // Build request parameters
    std::string params = "Action=AssumeRole"
                         "&RoleArn=" +
                         urlEncode(roleArn) + "&RoleSessionName=" + urlEncode(sessionName) +
                         "&Version=2011-06-15";
    if(!externalId.empty())
      params += "&ExternalId=" + urlEncode(externalId);

    // Make signed STS request with source credentials
    CURL* curl = curl_easy_init();
    if(!curl)
      return false;

    std::string response;
    std::string fullUrl = stsUrl + "/?" + params;
    curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlFetcher::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERNAME, sourceAccessKey.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, sourceSecretKey.c_str());

    std::string sigv4 = "aws:amz:" + region + ":sts";
    curl_easy_setopt(curl, CURLOPT_AWS_SIGV4, sigv4.c_str());

    struct curl_slist* headers = nullptr;
    if(!sourceSessionToken.empty())
    {
      headers = curl_slist_append(headers, ("x-amz-security-token: " + sourceSessionToken).c_str());
    }
    time_t now;
    time(&now);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y%m%dT%H%M%SZ", gmtime(&now));
    headers = curl_slist_append(headers, ("x-amz-date: " + std::string(date_buf)).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(res != CURLE_OK)
    {
      grklog.warn("S3Fetcher: STS AssumeRole request failed: %s", curl_easy_strerror(res));
      return false;
    }

    std::string accessKey = extractXmlValue(response, "AccessKeyId");
    std::string secretKey = extractXmlValue(response, "SecretAccessKey");
    std::string sessionToken = extractXmlValue(response, "SessionToken");
    std::string expiration = extractXmlValue(response, "Expiration");

    if(accessKey.empty() || secretKey.empty() || sessionToken.empty())
    {
      grklog.warn("S3Fetcher: STS AssumeRole returned incomplete credentials");
      return false;
    }

    auth_.username_ = accessKey;
    auth_.password_ = secretKey;
    auth_.session_token_ = sessionToken;

    cacheCredentials(AWSCredentialSource::ASSUMED_ROLE, accessKey, secretKey, sessionToken,
                     parseIso8601(expiration));
    {
      auto& c = cache();
      std::lock_guard<std::mutex> lock(c.mutex);
      c.roleArn = roleArn;
      c.externalId = externalId;
      c.roleSessionName = sessionName;
      c.sourceAccessKey = sourceAccessKey;
      c.sourceSecretKey = sourceSecretKey;
      c.sourceSessionToken = sourceSessionToken;
    }

    grklog.debug("S3Fetcher: assumed role %s", roleArn.c_str());
    return true;
  }

  // ─── 5. ECS container credentials ─────────────────────────────────

  bool tryECSCredentials()
  {
    auto fullUri = EnvVarManager::get("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    auto relativeUri = EnvVarManager::get("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");

    if((!fullUri || fullUri->empty()) && (!relativeUri || relativeUri->empty()))
      return false;

    std::string credUrl;
    if(fullUri && !fullUri->empty())
      credUrl = *fullUri;
    else
      credUrl = "http://169.254.170.2" + *relativeUri;

    // Authorization token
    std::string authHeader;
    auto tokenFile = EnvVarManager::get("AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE");
    auto tokenValue = EnvVarManager::get("AWS_CONTAINER_AUTHORIZATION_TOKEN");

    if(tokenFile && !tokenFile->empty())
    {
      std::string token;
      if(readFileContents(*tokenFile, token) && !token.empty())
      {
        trimWhitespace(token);
        authHeader = "Authorization: " + token;
      }
    }
    else if(tokenValue && !tokenValue->empty())
    {
      authHeader = "Authorization: " + *tokenValue;
    }

    std::string response = curlGet(credUrl, authHeader);
    if(response.empty())
    {
      grklog.debug("S3Fetcher: ECS credential fetch failed");
      return false;
    }

    // ECS returns JSON
    std::string accessKey = extractJsonString(response, "AccessKeyId");
    std::string secretKey = extractJsonString(response, "SecretAccessKey");
    std::string sessionToken = extractJsonString(response, "Token");
    std::string expiration = extractJsonString(response, "Expiration");

    if(accessKey.empty() || secretKey.empty())
    {
      grklog.debug("S3Fetcher: ECS credentials incomplete");
      return false;
    }

    auth_.username_ = accessKey;
    auth_.password_ = secretKey;
    auth_.session_token_ = sessionToken;
    resolveRegion(getProfile());

    cacheCredentials(AWSCredentialSource::EC2_OR_ECS, accessKey, secretKey, sessionToken,
                     parseIso8601(expiration));
    return true;
  }

  // ─── 6. EC2 instance metadata (IMDSv2 → v1 fallback) ─────────────

  bool tryEC2InstanceMetadata()
  {
    std::string ec2Root =
        EnvVarManager::get_string("GRK_AWS_EC2_API_ROOT_URL", "http://169.254.169.254");

    // IMDSv2: get session token via PUT
    std::string imdsToken;
    {
      CURL* curl = curl_easy_init();
      if(!curl)
        return false;

      std::string tokenUrl = ec2Root + "/latest/api/token";
      struct curl_slist* headers = nullptr;
      headers = curl_slist_append(headers, "X-aws-ec2-metadata-token-ttl-seconds: 21600");

      curl_easy_setopt(curl, CURLOPT_URL, tokenUrl.c_str());
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlFetcher::writeCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &imdsToken);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1L);

      CURLcode res = curl_easy_perform(curl);
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);

      if(res != CURLE_OK)
      {
        grklog.debug("S3Fetcher: IMDSv2 token request failed, trying IMDSv1 fallback");
        imdsToken.clear();

        // Verify metadata service is reachable at all (IMDSv1)
        std::string testResp;
        CURL* curl2 = curl_easy_init();
        if(curl2)
        {
          curl_easy_setopt(curl2, CURLOPT_URL, (ec2Root + "/latest/meta-data").c_str());
          curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, CurlFetcher::writeCallback);
          curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &testResp);
          curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 1L);
          res = curl_easy_perform(curl2);
          curl_easy_cleanup(curl2);
          if(res != CURLE_OK)
          {
            grklog.debug("S3Fetcher: not running on EC2 (metadata service unreachable)");
            return false;
          }
          grklog.debug("S3Fetcher: IMDSv2 unavailable, using IMDSv1");
        }
      }
    }

    // Get IAM role name
    std::string roleUrl = ec2Root + "/latest/meta-data/iam/security-credentials/";
    std::string roleName = curlGetWithToken(roleUrl, imdsToken, 1);
    if(roleName.empty())
    {
      grklog.debug("S3Fetcher: no IAM role found on instance");
      return false;
    }
    trimWhitespace(roleName);

    // Get credentials for role
    std::string response = curlGetWithToken(roleUrl + roleName, imdsToken, 5);
    if(response.empty())
    {
      grklog.debug("S3Fetcher: failed to get EC2 IAM credentials");
      return false;
    }

    // EC2 metadata returns JSON
    std::string accessKey = extractJsonString(response, "AccessKeyId");
    std::string secretKey = extractJsonString(response, "SecretAccessKey");
    std::string sessionToken = extractJsonString(response, "Token");
    std::string expiration = extractJsonString(response, "Expiration");

    if(accessKey.empty() || secretKey.empty())
    {
      grklog.debug("S3Fetcher: EC2 metadata returned incomplete credentials");
      return false;
    }

    auth_.username_ = accessKey;
    auth_.password_ = secretKey;
    auth_.session_token_ = sessionToken;
    resolveRegion(getProfile());

    cacheCredentials(AWSCredentialSource::EC2_OR_ECS, accessKey, secretKey, sessionToken,
                     parseIso8601(expiration));

    grklog.debug("S3Fetcher: obtained EC2 credentials, expiration: %s", expiration.c_str());
    return true;
  }

  // ─── SSO credentials ──────────────────────────────────────────────

  bool trySSOCredentials(const std::string& startUrl, const std::string& ssoSession,
                         const std::string& accountId, const std::string& roleName)
  {
    if(accountId.empty() || roleName.empty())
    {
      grklog.warn("S3Fetcher: SSO requires sso_account_id and sso_role_name");
      return false;
    }

    std::string awsDir = getAWSRootDir();
    if(awsDir.empty())
      return false;

    std::string cacheDir = awsDir + "/sso/cache";
    std::string accessToken;
    std::string ssoRegion;

    // Scan SSO cache for a file with matching startUrl
    try
    {
      for(auto& entry : std::filesystem::directory_iterator(cacheDir))
      {
        if(!entry.is_regular_file() || entry.path().extension() != ".json")
          continue;

        std::string contents;
        if(!readFileContents(entry.path().string(), contents))
          continue;

        std::string gotStartUrl = extractJsonString(contents, "startUrl");
        if(gotStartUrl.empty())
          continue;

        bool match = false;
        if(!startUrl.empty() && gotStartUrl == startUrl)
          match = true;
        if(!ssoSession.empty() && !startUrl.empty() && gotStartUrl == startUrl)
          match = true;

        if(!match)
          continue;

        // Verify token hasn't expired
        std::string expiresAt = extractJsonString(contents, "expiresAt");
        if(!expiresAt.empty())
        {
          time_t expTime = parseIso8601(expiresAt);
          time_t now;
          time(&now);
          if(now > expTime)
          {
            grklog.warn("S3Fetcher: SSO token expired at %s. Run 'aws sso login'.",
                        expiresAt.c_str());
            continue;
          }
        }

        accessToken = extractJsonString(contents, "accessToken");
        ssoRegion = extractJsonString(contents, "region");
        if(!accessToken.empty())
          break;
      }
    }
    catch(const std::exception& e)
    {
      grklog.debug("S3Fetcher: cannot scan SSO cache: %s", e.what());
      return false;
    }

    if(accessToken.empty())
    {
      grklog.debug("S3Fetcher: no valid SSO token in cache");
      return false;
    }

    if(ssoRegion.empty())
      ssoRegion = "us-east-1";

    // GetRoleCredentials request
    bool https = useHttps();
    std::string defaultHost = "portal.sso." + ssoRegion + ".amazonaws.com";
    std::string ssoHost = EnvVarManager::get_string("GRK_AWS_SSO_ENDPOINT", defaultHost);

    std::string ssoUrl = (https ? "https://" : "http://") + ssoHost +
                         "/federation/credentials?role_name=" + urlEncode(roleName) +
                         "&account_id=" + urlEncode(accountId);

    std::string response = curlGet(ssoUrl, "x-amz-sso_bearer_token: " + accessToken);
    if(response.empty())
    {
      grklog.warn("S3Fetcher: SSO GetRoleCredentials failed");
      return false;
    }

    // Response JSON: {"roleCredentials":{"accessKeyId":"...","secretAccessKey":"...", ...}}
    std::string ak = extractJsonString(response, "accessKeyId");
    std::string sk = extractJsonString(response, "secretAccessKey");
    std::string st = extractJsonString(response, "sessionToken");
    std::string expirationMs = extractJsonString(response, "expiration");

    if(ak.empty() || sk.empty() || st.empty())
    {
      grklog.warn("S3Fetcher: SSO returned incomplete credentials");
      return false;
    }

    auth_.username_ = ak;
    auth_.password_ = sk;
    auth_.session_token_ = st;
    resolveRegion(getProfile());

    time_t expTime = 0;
    if(!expirationMs.empty())
    {
      try
      {
        expTime = std::stoll(expirationMs) / 1000;
      }
      catch(...)
      {}
    }

    cacheCredentials(AWSCredentialSource::SSO, ak, sk, st, expTime);
    {
      auto& c = cache();
      std::lock_guard<std::mutex> lock(c.mutex);
      c.ssoStartURL = startUrl;
      c.ssoAccountId = accountId;
      c.ssoRoleName = roleName;
    }

    grklog.debug("S3Fetcher: obtained SSO credentials");
    return true;
  }

  // ─── credential_process ───────────────────────────────────────────

  bool tryCredentialProcess(const std::string& command)
  {
    if(command.empty())
      return false;

    grklog.debug("S3Fetcher: executing credential_process: %s", command.c_str());

    FILE* pipe = popen(command.c_str(), "r");
    if(!pipe)
    {
      grklog.warn("S3Fetcher: failed to execute credential_process: %s", command.c_str());
      return false;
    }

    std::string output;
    char buffer[256];
    while(fgets(buffer, sizeof(buffer), pipe))
      output += buffer;

    int exitCode = pclose(pipe);
    if(exitCode != 0)
    {
      grklog.warn("S3Fetcher: credential_process exited with code %d", exitCode);
      return false;
    }

    if(output.empty())
    {
      grklog.warn("S3Fetcher: credential_process returned empty output");
      return false;
    }

    std::string version = extractJsonString(output, "Version");
    if(version != "1")
    {
      grklog.warn("S3Fetcher: credential_process Version '%s' unsupported (expected '1')",
                  version.c_str());
      return false;
    }

    std::string ak = extractJsonString(output, "AccessKeyId");
    std::string sk = extractJsonString(output, "SecretAccessKey");
    std::string st = extractJsonString(output, "SessionToken");
    std::string expiration = extractJsonString(output, "Expiration");

    if(ak.empty() || sk.empty())
    {
      grklog.warn(
          "S3Fetcher: credential_process did not return required AccessKeyId/SecretAccessKey");
      return false;
    }

    auth_.username_ = ak;
    auth_.password_ = sk;
    auth_.session_token_ = st;
    resolveRegion(getProfile());

    cacheCredentials(AWSCredentialSource::CREDENTIAL_PROCESS, ak, sk, st,
                     expiration.empty() ? 0 : parseIso8601(expiration));
    {
      auto& c = cache();
      std::lock_guard<std::mutex> lock(c.mutex);
      c.credentialProcess = command;
    }

    grklog.debug("S3Fetcher: obtained credentials from credential_process");
    return true;
  }

  // ═══════════════════════════════════════════════════════════════════
  //  URL / endpoint helpers
  // ═══════════════════════════════════════════════════════════════════

  bool useHttps() const
  {
    if(auth_.s3_use_https_ != 0)
      return auth_.s3_use_https_ > 0;
    return EnvVarManager::get_string("AWS_HTTPS", "YES") != "NO";
  }

  static std::string formatPort(int port, bool https)
  {
    int defaultPort = https ? 443 : 80;
    return port != defaultPort ? ":" + std::to_string(port) : "";
  }

  bool isVirtualHostingEnabled() const
  {
    if(auth_.s3_use_virtual_hosting_ != 0)
      return auth_.s3_use_virtual_hosting_ > 0;
    return EnvVarManager::test_bool("AWS_VIRTUAL_HOSTING");
  }

  void configureEndpoint(ParsedFetchPath& parsed, bool useVirtualHosting)
  {
    bool https = useHttps();
    int defaultPort = https ? 443 : 80;

    // Check pre-configured endpoint first, then env var
    std::optional<std::string> s3Endpoint;
    if(!auth_.s3_endpoint_.empty())
      s3Endpoint = auth_.s3_endpoint_;
    else
      s3Endpoint = EnvVarManager::get("AWS_S3_ENDPOINT");

    if(s3Endpoint)
    {
      std::string endpoint = *s3Endpoint;
      if(endpoint.starts_with("https://"))
        endpoint = endpoint.substr(8);
      else if(endpoint.starts_with("http://"))
        endpoint = endpoint.substr(7);

      size_t portPos = endpoint.find(':');
      if(portPos != std::string::npos)
      {
        parsed.host = endpoint.substr(0, portPos);
        try
        {
          parsed.port = std::stoi(endpoint.substr(portPos + 1));
        }
        catch(...)
        {
          parsed.port = defaultPort;
        }
      }
      else
      {
        parsed.host = endpoint;
        parsed.port = defaultPort;
      }

      if(useVirtualHosting)
        parsed.host = parsed.bucket + "." + parsed.host;
    }
    else
    {
      parsed.host = useVirtualHosting ? parsed.bucket + ".s3." + auth_.region_ + ".amazonaws.com"
                                      : "s3." + auth_.region_ + ".amazonaws.com";
      parsed.port = defaultPort;
    }
  }

  bool handleVirtualHosting(const std::string& url, ParsedFetchPath& parsed)
  {
    if(!isVirtualHostingEnabled())
      return false;

    // Strip protocol
    std::string urlBody = url;
    if(urlBody.starts_with("https://"))
      urlBody = urlBody.substr(8);
    else if(urlBody.starts_with("http://"))
      urlBody = urlBody.substr(7);

    // Extract host
    size_t slashPos = urlBody.find('/');
    if(slashPos == std::string::npos)
      return false;

    std::string hostPort = urlBody.substr(0, slashPos);
    std::string pathPart = urlBody.substr(slashPos + 1);

    // Strip port from host
    std::string host = hostPort;
    size_t colonPos = hostPort.find(':');
    if(colonPos != std::string::npos)
    {
      host = hostPort.substr(0, colonPos);
      try
      {
        parsed.port = std::stoi(hostPort.substr(colonPos + 1));
      }
      catch(...)
      {
        parsed.port = useHttps() ? 443 : 80;
      }
    }

    // Determine S3 endpoint suffix (check pre-configured first, then env var)
    std::string s3Endpoint;
    if(!auth_.s3_endpoint_.empty())
      s3Endpoint = auth_.s3_endpoint_;
    else
      s3Endpoint =
          EnvVarManager::get_string("AWS_S3_ENDPOINT", "s3." + auth_.region_ + ".amazonaws.com");
    if(s3Endpoint.starts_with("https://"))
      s3Endpoint = s3Endpoint.substr(8);
    else if(s3Endpoint.starts_with("http://"))
      s3Endpoint = s3Endpoint.substr(7);
    colonPos = s3Endpoint.find(':');
    if(colonPos != std::string::npos)
      s3Endpoint = s3Endpoint.substr(0, colonPos);

    // Check if host is bucket.endpoint
    if(host.ends_with(s3Endpoint))
    {
      size_t bucketLen = host.length() - s3Endpoint.length() - 1;
      if(bucketLen > 0 && host[bucketLen] == '.')
      {
        parsed.bucket = host.substr(0, bucketLen);
        parsed.host = host;
        parsed.key = pathPart;
        grklog.debug("S3Fetcher: detected virtual-hosted URL: bucket=%s", parsed.bucket.c_str());
        return true;
      }
    }
    return false;
  }

  // Parse http:// URLs (similar to FetchPathParser::parseHttpsPath but for http)
  static void parseHttpUrl(std::string& url, ParsedFetchPath& parsed)
  {
    if(!url.starts_with("http://"))
      throw std::runtime_error("Invalid HTTP URL");
    url = url.substr(7);

    size_t slashPos = url.find('/');
    if(slashPos == std::string::npos)
      throw std::runtime_error("Invalid HTTP URL: no path");

    std::string hostPort = url.substr(0, slashPos);
    std::string path = url.substr(slashPos + 1);

    size_t colonPos = hostPort.find(':');
    if(colonPos != std::string::npos)
    {
      parsed.host = hostPort.substr(0, colonPos);
      try
      {
        parsed.port = std::stoi(hostPort.substr(colonPos + 1));
      }
      catch(...)
      {
        parsed.port = 80;
      }
    }
    else
    {
      parsed.host = hostPort;
      parsed.port = 80;
    }

    size_t bucketEnd = path.find('/');
    if(bucketEnd == std::string::npos)
      throw std::runtime_error("Invalid HTTP URL: no key after bucket");
    parsed.bucket = path.substr(0, bucketEnd);
    parsed.key = path.substr(bucketEnd + 1);
  }

  // ═══════════════════════════════════════════════════════════════════
  //  Utility helpers
  // ═══════════════════════════════════════════════════════════════════

  void resolveRegion(const std::string& profile)
  {
    if(auto r = EnvVarManager::get("AWS_REGION"))
    {
      auth_.region_ = *r;
      return;
    }
    if(auto r = EnvVarManager::get("AWS_DEFAULT_REGION"))
    {
      auth_.region_ = *r;
      return;
    }

    // Try config file
    if(auth_.region_.empty())
    {
      std::string configFile = getConfigFilePath();
      if(!configFile.empty())
      {
        IniParser parser;
        if(parser.parse(configFile))
        {
          auto it = parser.sections.find("profile " + profile);
          if(it == parser.sections.end())
            it = parser.sections.find(profile);
          if(it != parser.sections.end())
          {
            auto regionIt = it->second.find("region");
            if(regionIt != it->second.end() && !regionIt->second.empty())
            {
              auth_.region_ = regionIt->second;
              return;
            }
          }
        }
      }
    }

    if(auth_.region_.empty())
      auth_.region_ = "us-east-1";
  }

  static std::string getProfile()
  {
    if(auto p = EnvVarManager::get("AWS_PROFILE"))
      return *p;
    if(auto p = EnvVarManager::get("AWS_DEFAULT_PROFILE"))
      return *p;
    return "default";
  }

  static std::string getConfigFilePath()
  {
    if(auto p = EnvVarManager::get("AWS_CONFIG_FILE"))
      return *p;
#ifdef _WIN32
    if(auto p = EnvVarManager::get("USERPROFILE"))
      return *p + "\\.aws\\config";
#else
    if(auto p = EnvVarManager::get("HOME"))
      return *p + "/.aws/config";
#endif
    return {};
  }

  static std::string getCredentialsFilePath()
  {
    if(auto p = EnvVarManager::get("GRK_AWS_CREDENTIALS_FILE"))
      return *p;
#ifdef _WIN32
    if(auto p = EnvVarManager::get("USERPROFILE"))
      return *p + "\\.aws\\credentials";
#else
    if(auto p = EnvVarManager::get("HOME"))
      return *p + "/.aws/credentials";
#endif
    return {};
  }

  static std::string getAWSRootDir()
  {
    if(auto p = EnvVarManager::get("GRK_AWS_ROOT_DIR"))
      return *p;
#ifdef _WIN32
    if(auto p = EnvVarManager::get("USERPROFILE"))
      return *p + "\\.aws";
#else
    if(auto p = EnvVarManager::get("HOME"))
      return *p + "/.aws";
#endif
    return {};
  }

  std::string buildSTSUrl()
  {
    std::string regional = EnvVarManager::get_string("AWS_STS_REGIONAL_ENDPOINTS", "regional");
    std::string stsUrl;
    if(regional == "regional")
    {
      std::string region = EnvVarManager::get_string(
          "AWS_REGION", EnvVarManager::get_string("AWS_DEFAULT_REGION", "us-east-1"));
      stsUrl = "https://sts." + region + ".amazonaws.com";
    }
    else
    {
      stsUrl = "https://sts.amazonaws.com";
    }
    if(auto endpoint = EnvVarManager::get("GRK_AWS_STS_ROOT_URL"))
      stsUrl = *endpoint;
    return stsUrl;
  }

  void cacheCredentials(AWSCredentialSource source, const std::string& ak, const std::string& sk,
                        const std::string& st, time_t expiration)
  {
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    c.source = source;
    c.accessKey = ak;
    c.secretKey = sk;
    c.sessionToken = st;
    c.region = auth_.region_;
    c.expiration = expiration;
  }

  // ─── HTTP helpers ─────────────────────────────────────────────────

  static std::string curlGet(const std::string& url, const std::string& header = "")
  {
    CURL* curl = curl_easy_init();
    if(!curl)
      return {};

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlFetcher::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    struct curl_slist* headers = nullptr;
    if(!header.empty())
    {
      headers = curl_slist_append(headers, header.c_str());
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if(headers)
      curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK ? response : std::string{};
  }

  static std::string curlGetWithToken(const std::string& url, const std::string& token,
                                      long timeout = 5)
  {
    CURL* curl = curl_easy_init();
    if(!curl)
      return {};

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlFetcher::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

    struct curl_slist* headers = nullptr;
    if(!token.empty())
    {
      headers = curl_slist_append(headers, ("X-aws-ec2-metadata-token: " + token).c_str());
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if(headers)
      curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK ? response : std::string{};
  }

  // ─── Parsing helpers ──────────────────────────────────────────────

  static std::string extractXmlValue(const std::string& xml, const std::string& tag)
  {
    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";
    size_t pos = xml.find(openTag);
    if(pos == std::string::npos)
      return {};
    pos += openTag.length();
    size_t end = xml.find(closeTag, pos);
    if(end == std::string::npos)
      return {};
    return xml.substr(pos, end - pos);
  }

  static std::string extractJsonString(const std::string& json, const std::string& key)
  {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if(pos == std::string::npos)
      return {};
    pos += pattern.length();

    // Skip whitespace and colon
    while(pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':'))
      pos++;

    if(pos >= json.size())
      return {};

    if(json[pos] == '"')
    {
      // Quoted string value
      pos++;
      size_t end = pos;
      while(end < json.size() && json[end] != '"')
      {
        if(json[end] == '\\')
          end++; // skip escaped char
        end++;
      }
      return json.substr(pos, end - pos);
    }

    // Numeric or unquoted value
    size_t end = pos;
    while(end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ' &&
          json[end] != '\n')
      end++;
    return json.substr(pos, end - pos);
  }

  static bool readFileContents(const std::string& path, std::string& contents)
  {
    std::ifstream file(path, std::ios::binary);
    if(!file.is_open())
      return false;
    contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  }

  static void trimWhitespace(std::string& s)
  {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
  }

  static time_t parseIso8601(const std::string& str)
  {
    if(str.empty())
      return 0;

    struct tm tm = {};
    if(sscanf(str.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
              &tm.tm_min, &tm.tm_sec) >= 6)
    {
      tm.tm_year -= 1900;
      tm.tm_mon -= 1;
#ifdef _WIN32
      return _mkgmtime(&tm);
#else
      return timegm(&tm);
#endif
    }
    return 0;
  }

  static std::string urlEncode(const std::string& str)
  {
    CURL* curl = curl_easy_init();
    if(!curl)
      return str;
    char* encoded = curl_easy_escape(curl, str.c_str(), (int)str.length());
    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
  }
};

} // namespace grk

#endif
