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

#include "mqc_backup.h"

namespace grk::t1
{

mqcoder_backup::mqcoder_backup(void)
    : mqcoder_base(true), flagsBackup_(nullptr), position(BACKUP_DISABLED), i(BACKUP_DISABLED),
      j(BACKUP_DISABLED), k(BACKUP_DISABLED), partial(false), runlen(BACKUP_DISABLED),
      dataPtr_(nullptr), flagsPtr_(nullptr), _flags(0), numBpsToDecompress_(BACKUP_DISABLED),
      passno_(BACKUP_DISABLED), passtype_(BACKUP_DISABLED), layer_(BACKUP_DISABLED)
{}

/**
 * @brief Destroys a mqcoder_backup
 */
mqcoder_backup::~mqcoder_backup()
{
  grk_aligned_free(flagsBackup_);
  uncompressedBufBackup_.dealloc();
}

void mqcoder_backup::print(const std::string& msg)
{
  mqcoder_base::print(msg);
  printf(" : position: %d, i: %d, j: %d, k: %d, flagsPtr: %p, flags: 0x%x\n : partial: %d "
         "runlen: %d\n : passno: %d,  passtype: %d,  numBpsToDecompress: %d, layer: %d\n",
         position, i, j, k, flagsPtr_, _flags, partial, runlen, passno_, passtype_,
         numBpsToDecompress_, layer_);
}

// Copy constructor
mqcoder_backup::mqcoder_backup(const mqcoder_backup& other) : mqcoder_base(other)
{
  *this = other;
}

// Assignment operator for mqcoder_backup
mqcoder_backup& mqcoder_backup::operator=(const mqcoder_backup& other)
{
  if(this != &other)
  {
    // Call the base class assignment operator
    mqcoder_base::operator=(other);

    flagsBackup_ = nullptr;
    uncompressedBufBackup_ = nullptr;
    position = other.position;
    i = other.i;
    j = other.j;
    k = other.k;
    partial = other.partial;
    runlen = other.runlen;
    dataPtr_ = nullptr;
    flagsPtr_ = nullptr;
    _flags = other._flags;
    numBpsToDecompress_ = other.numBpsToDecompress_;
    passno_ = other.passno_;
    passtype_ = other.passtype_;
    layer_ = other.layer_;
  }
  return *this;
}

bool mqcoder_backup::operator==(const mqcoder_backup& other) const
{
  if(!mqcoder_base::operator==(other))
  { // Compare base class members
    return false;
  }

  if(position != other.position || i != other.i || j != other.j || k != other.k ||
     partial != other.partial || runlen != other.runlen || _flags != other._flags ||
     numBpsToDecompress_ != other.numBpsToDecompress_ || passno_ != other.passno_ ||
     passtype_ != other.passtype_ || layer_ != other.layer_)
  {
    return false;
  }

  return true;
}

} // namespace grk::t1
