/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

namespace grk
{
class CompressScheduler : public Scheduler
{
  public:
	CompressScheduler(Tile* tile, bool needsRateControl);
	~CompressScheduler() = default;
	void scheduleCompress(TileCodingParams* tcp, const double* mct_norms, uint16_t mct_numcomps);
	void compress(std::vector<CompressBlockExec*>* blocks);

  private:
	bool compress(size_t threadId, uint64_t maxBlocks);
	void compress(T1Interface* impl, CompressBlockExec* block);

	Tile* tile;
	mutable std::mutex distortion_mutex;
	bool needsRateControl;
	CompressBlockExec** encodeBlocks;
	std::atomic<int64_t> blockCount;
};

} // namespace grk
