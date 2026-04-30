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

// Stub implementation — freebyrd thread pool has been removed.
// SchedulerFreebyrd remains as a skeleton so the build succeeds but
// setting GRK_SCHEDULER=freebyrd will log an error and fall back.

#include "Logger.h"
#include "SchedulerFreebyrd.h"

namespace grk
{

SchedulerFreebyrd::SchedulerFreebyrd(uint16_t numcomps, uint8_t prec)
    : numcomps_(numcomps), prec_(prec), success_(true)
{}

SchedulerFreebyrd::~SchedulerFreebyrd()
{
  release();
}

void SchedulerFreebyrd::release()
{
  blocksByComp_.clear();
}

bool SchedulerFreebyrd::decompressTile([[maybe_unused]] ITileProcessor* tileProcessor)
{
  grklog.error("SchedulerFreebyrd: freebyrd backend has been removed. "
               "Unset GRK_SCHEDULER or use the default scheduler.");
  return false;
}

bool SchedulerFreebyrd::decodeBlocks([[maybe_unused]] ITileProcessor* tileProcessor)
{
  return false;
}

bool SchedulerFreebyrd::runDWT([[maybe_unused]] ITileProcessor* tileProcessor)
{
  return false;
}

bool SchedulerFreebyrd::runCascadeDWT97([[maybe_unused]] ITileProcessor* tileProcessor,
                                        [[maybe_unused]] uint16_t compno)
{
  return false;
}

bool SchedulerFreebyrd::runSeparateDWT53([[maybe_unused]] ITileProcessor* tileProcessor,
                                         [[maybe_unused]] uint16_t compno)
{
  return false;
}

bool SchedulerFreebyrd::runSeparateDWT16([[maybe_unused]] ITileProcessor* tileProcessor,
                                         [[maybe_unused]] uint16_t compno)
{
  return false;
}

bool SchedulerFreebyrd::postProcess([[maybe_unused]] ITileProcessor* tileProcessor)
{
  return false;
}

} // namespace grk
