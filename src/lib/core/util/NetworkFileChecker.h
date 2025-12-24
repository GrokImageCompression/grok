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

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#include <sys/sysmacros.h> // For major() and minor() macros
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace grk
{

class NetworkFileChecker
{
public:
  NetworkFileChecker() = default;

  // Check if the file is on iSCSI storage with caching
  bool isIscsi(const std::string& filePath)
  {
    bool result = false;
#ifndef _WIN32
    struct stat fileStat;
    if(stat(filePath.c_str(), &fileStat) != 0)
    {
      perror("stat");
      return false;
    }

    // Get the major and minor device numbers from the file's stat
    dev_t device = fileStat.st_dev;
    auto majorNum = major(device);
    auto minorNum = minor(device);

    // Get the device path
    std::string devicePath = getDevicePath(majorNum, minorNum);
    if(devicePath.empty())
    {
      std::cerr << "Failed to find the device path.\n";
      return false;
    }

    // Check the cache first
    if(deviceCache.find(devicePath) != deviceCache.end())
    {
      return deviceCache[devicePath];
    }

    // Perform the iSCSI check and cache the result
    result = isIscsiDevice(devicePath);
    deviceCache[devicePath] = result;
#endif
    return result;
  }

  // Get block size if the file is on iSCSI storage
  int getBlockSize(const std::string& filePath)
  {
#ifndef _WIN32
    if(isIscsi(filePath))
    {
      struct stat fileStat;
      if(stat(filePath.c_str(), &fileStat) != 0)
      {
        perror("stat");
        return -1;
      }

      // Get the major and minor device numbers
      dev_t device = fileStat.st_dev;
      auto majorNum = major(device);
      auto minorNum = minor(device);

      // Find the block device path
      std::string devicePath = getDevicePath(majorNum, minorNum);
      if(devicePath.empty())
        return -1;

      // Read the block size from /sys/class/block/<device>/queue/logical_block_size
      std::string blockSizePath =
          "/sys/class/block/" + getDeviceName(devicePath) + "/queue/logical_block_size";
      std::ifstream blockSizeFile(blockSizePath);
      int blockSize = -1;
      if(blockSizeFile >> blockSize)
      {
        return blockSize;
      }
      std::cerr << "Failed to get block size.\n";
      return -1;
    }
#endif

    std::cerr << "The file is not on iSCSI storage.\n";
    return -1;
  }

  // Get optimal fetch size from /sys/class/block/<device>/queue/optimal_io_size
  int getOptimalFetchSize(const std::string& filePath)
  {
#ifndef _WIN32
    struct stat fileStat;
    if(stat(filePath.c_str(), &fileStat) != 0)
    {
      perror("stat");
      return -1;
    }

    // Get the major and minor device numbers
    dev_t device = fileStat.st_dev;
    auto majorNum = major(device);
    auto minorNum = minor(device);

    // Find the block device path
    std::string devicePath = getDevicePath(majorNum, minorNum);
    if(devicePath.empty())
      return -1;

    // Read the optimal I/O size from /sys/class/block/<device>/queue/optimal_io_size
    std::string optimalSizePath =
        "/sys/class/block/" + getDeviceName(devicePath) + "/queue/optimal_io_size";
    std::ifstream optimalSizeFile(optimalSizePath);
    int optimalSize = -1;
    if(optimalSizeFile >> optimalSize)
    {
      return optimalSize;
    }
#endif
    std::cerr << "Failed to get optimal fetch size.\n";
    return -1;
  }

  bool isNetworkDrive(const std::string& filePath)
  {
#ifndef _WIN32
    // Check if it's an iSCSI device
    if(isIscsi(filePath))
    {
      return true;
    }

    // Check /proc/mounts for network file systems like NFS or CIFS
    std::ifstream mounts("/proc/mounts");
    std::string line;

    while(std::getline(mounts, line))
    {
      std::istringstream iss(line);
      std::string devPath, mountPoint, fsType;

      iss >> devPath >> mountPoint >> fsType;

      // If the file path matches the mount point and the file system is NFS or CIFS
      if(filePath.find(mountPoint) == 0 && (fsType == "nfs" || fsType == "cifs"))
      {
        return true; // File is on a network file system
      }
    }
#endif
    return false; // File is on local storage
  }

private:
  // Caching whether a device is iSCSI
  std::unordered_map<std::string, bool> deviceCache;

  // Helper function to get device path from major/minor number
  std::string getDevicePath(uint32_t majorNum, uint32_t minorNum)
  {
#ifndef _WIN32
    std::ifstream mounts("/proc/mounts");
    std::string line;

    while(std::getline(mounts, line))
    {
      std::istringstream iss(line);
      std::string devPath, mountPoint;

      iss >> devPath >> mountPoint;

      struct stat devStat;
      if(stat(devPath.c_str(), &devStat) == 0)
      {
        if(major(devStat.st_rdev) == majorNum && minor(devStat.st_rdev) == minorNum)
        {
          return devPath;
        }
      }
    }
#endif
    return "";
  }

  // Check if the device is part of an iSCSI session
  bool isIscsiDevice(const std::string& devicePath)
  {
    // Read the /sys/class/block/<device>/device/modalias to check for "iscsi"
    std::string modaliasPath = "/sys/class/block/" + getDeviceName(devicePath) + "/device/modalias";
    std::ifstream modaliasFile(modaliasPath);
    std::string modalias;
    if(modaliasFile >> modalias)
    {
      return modalias.find("scsi") != std::string::npos;
    }
    return false;
  }

  // Helper function to extract the device name from the device path
  std::string getDeviceName(const std::string& devicePath)
  {
    size_t pos = devicePath.find_last_of('/');
    if(pos != std::string::npos)
    {
      return devicePath.substr(pos + 1);
    }
    return devicePath;
  }
};

} // namespace grk
