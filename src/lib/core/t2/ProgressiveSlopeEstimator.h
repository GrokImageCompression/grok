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

/**
 * @file ProgressiveSlopeEstimator.h
 * @brief Intra-frame progressive slope threshold estimation for early pass termination.
 *
 * OVERVIEW
 * ========
 *
 * In JPEG 2000 compression, rate control is traditionally a two-phase process:
 *
 *   Phase 1 (T1): Encode ALL code blocks completely, generating all bit-planes and
 *                 their associated coding passes with rate/distortion information.
 *
 *   Phase 2 (PCRD): Post-Compression Rate-Distortion optimization searches for
 *                   a slope threshold λ that achieves the target file size.
 *
 * This class enables a performance optimization: during Phase 1, as code blocks
 * complete, we progressively build a statistical model of the slope distribution.
 * Once enough blocks have completed, we can PREDICT the final PCRD threshold with
 * high confidence. Subsequent blocks can then terminate encoding early — skipping
 * bit-planes whose coding passes are almost certainly going to be discarded by PCRD.
 *
 * MATHEMATICAL FOUNDATION
 * =======================
 *
 * JPEG 2000's rate control is based on Lagrangian optimization (Taubman & Marcellin,
 * Chapter 8). The key insight is:
 *
 *   Given independent code blocks B_1, ..., B_N, each with coding passes generating
 *   a rate-distortion curve, the globally optimal truncation (minimum distortion at
 *   target rate R) is achieved by a SINGLE slope threshold λ* such that:
 *
 *     Σ R_n(λ*) ≈ R_target
 *
 *   where R_n(λ) is the rate of block n when truncated at slope λ.
 *
 * This means λ* depends on the AGGREGATE rate-slope distribution across ALL blocks.
 * Once we observe a sufficient fraction of blocks, the aggregate distribution
 * stabilizes (by the law of large numbers), and λ* can be predicted.
 *
 * SLOPE HISTOGRAM
 * ===============
 *
 * We maintain a histogram H[s] where s ∈ [0, 2047] is a quantized slope index:
 *
 *   s = max(0, (log_slope >> 4) - 2048)
 *
 * where log_slope is the 16-bit logarithmic slope representation used by JPEG 2000
 * (see RateControl::slopeToLog). H[s] accumulates the total byte count of all
 * coding passes whose slope falls into bin s.
 *
 * Given the histogram and a target rate R_target, we find the threshold:
 *
 *   λ_est = min { s : Σ_{t=s}^{2047} H[t] ≥ R_adjusted }
 *
 * where R_adjusted accounts for the fraction of blocks not yet encoded.
 *
 * CONSERVATIVE ESTIMATION
 * =======================
 *
 * A pass whose slope sits just under the PCRD threshold removes almost no distortion,
 * so a threshold a little above the true one costs nothing measurable. What must not
 * happen is a threshold far above it.
 *
 * Three things shape the published threshold:
 *
 *   1. REPRESENTATIVE SAMPLE: The scheduler builds its block list component by
 *      component with resolution 0 first, so it shuffles that list into a fixed
 *      random order before dispatching. Every subband is then represented in
 *      proportion to its size from the first blocks onward.
 *
 *   2. PADDING FACTOR: We add a padding term when extrapolating from partial data:
 *
 *        R_adjusted = R_target × (coded_samples + padding) / total_samples
 *
 *      where padding = max(kMinimumSamplePadding, total_samples / kSamplePaddingDivisor).
 *      It holds the threshold down while the coded fraction is small and the estimate
 *      is still noisy, and decays as the coded fraction grows.
 *
 *   3. THRESHOLD SCALING: the published threshold is kDefaultThresholdScale times the
 *      estimate.
 *
 * MULTI-THREADED OPERATION
 * ========================
 *
 * In Grok's parallel T1 encoding (via TaskFlow), multiple threads encode blocks
 * simultaneously. The estimator uses:
 *
 *   - Atomic uint16_t for the published threshold (lock-free read by all threads)
 *   - Mutex-protected histogram updates (infrequent, amortized)
 *   - Exponential backoff on update frequency: threads update the master histogram
 *     every K blocks (K doubles up to 16), reducing contention
 *
 * The update frequency is tuned so that:
 *   - With 8-16 threads encoding thousands of blocks, contention is negligible
 *   - The threshold stabilizes within the first ~20% of blocks
 *   - Total overhead is < 0.1% of T1 encoding time
 *
 * EXPECTED PERFORMANCE GAIN
 * =========================
 *
 * For DCI (Digital Cinema) encoding at 250 Mbps / 24fps (≈10.4 MB/frame):
 *   - Typical frames have 5-7 significant bit-planes per code block
 *   - At the target rate, PCRD retains only the top 2-4 bit-planes
 *   - The bottom 2-5 bit-planes are ALWAYS discarded
 *   - Early termination skips encoding these bit-planes entirely
 *   - Expected T1 compute reduction: 20-40%
 *
 * For lower bitrates, the savings are even greater (more passes discarded).
 * For near-lossless or high-bitrate encoding, savings are minimal (most passes retained).
 *
 * LIMITATIONS
 * ===========
 *
 * 1. This optimization is ONLY applicable when a target rate is specified
 *    (rate-distortion allocation mode). It does not apply to lossless encoding
 *    or quality-based allocation.
 *
 * 2. The first ~20% of blocks are encoded without early termination (insufficient
 *    statistics). The savings apply only to the remaining ~80%.
 *
 * 3. In pathological cases (extremely non-stationary images, e.g., half black /
 *    half noise), the conservative padding prevents quality loss but reduces
 *    the performance benefit.
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <limits>

namespace grk
{

/**
 * @brief Number of quantized slope bins in the histogram.
 *
 * The 16-bit log-slope range [0, 65535] is quantized to 2048 bins:
 *   bin = max(0, (log_slope >> kSlopeBinShift) - 2048)
 *
 * This gives ~0.4 dB resolution per bin, which is more than sufficient
 * for threshold estimation.
 */
static constexpr int kSlopeBins = 2048;

/** @brief Log-slope units per histogram bin, as a shift and as a width. */
static constexpr int kSlopeBinShift = 4;
static constexpr int kSlopeBinWidth = 1 << kSlopeBinShift;

/** @brief Log-slope units per octave of linear slope, from RateControl::slopeToLog. */
static constexpr double kLogSlopeUnitsPerOctave = 256.0;

/**
 * @brief Scale applied to the estimated threshold before it is published.
 *
 * Above 1 the published threshold sits above the estimate. The passes it skips
 * have a slope just under PCRD's own threshold and remove orders of magnitude less
 * distortion than the passes kept, so the reference frames lose nothing measurable
 * until the scale reaches 2.5.
 */
static constexpr double kDefaultThresholdScale = 1.5;

/**
 * @brief Minimum fraction of total samples that must be encoded before
 *        the estimator begins publishing a threshold.
 *
 * Below this fraction, the sample size is too small for reliable estimation.
 * The value 0.05 (5%) means we need at least 5% of all subband samples encoded.
 * For a typical 4K DCI frame with ~8M samples, this is ~400K samples or roughly
 * 100-200 code blocks (depending on block size).
 */
static constexpr double kMinSampleFraction = 0.05;

/** @brief Phantom samples added to the coded total when extrapolating the byte budget. */
static constexpr uint64_t kMinimumSamplePadding = 4096;
static constexpr uint64_t kSamplePaddingDivisor = 64;

/**
 * @class ProgressiveSlopeEstimator
 * @brief Estimates the PCRD slope threshold progressively during T1 encoding.
 *
 * Thread-safe for concurrent use by multiple encoder threads.
 *
 * Usage:
 *   1. Construct with total sample count and target rate (bytes/sample)
 *   2. After each block completes, call updateStats() with its pass data
 *   3. Before starting each block, read getEarlyStopSlope() for the threshold
 */
class ProgressiveSlopeEstimator
{
public:
  /**
   * @brief Construct the estimator.
   *
   * @param totalSamples Total number of subband samples in the tile/frame.
   *                     This is the sum of all code block areas across all
   *                     subbands, resolutions, and components.
   *
   * @param targetRate Target compressed size in bytes per sample.
   *                   Computed as: targetBytes / totalSamples.
   *                   For DCI 4K at 10.4 MB/frame with ~8.8M samples: ~1.18 bytes/sample.
   *
   * @param thresholdScale Scale applied to the estimate before publishing it.
   *                       Lower values terminate fewer passes.
   */
  ProgressiveSlopeEstimator(uint64_t totalSamples, double targetRate,
                            double thresholdScale = kDefaultThresholdScale)
      : totalSamples_(totalSamples), targetRate_(targetRate), thresholdScale_(thresholdScale),
        codedSamples_(0), minBin_(kSlopeBins - 1), maxBin_(0), currentThreshold_(0),
        updateCounter_(0), nextUpdateInterval_(2)
  {
    // phantom samples assumed to have average compressibility
    conservativePadding_ =
        std::max<uint64_t>(kMinimumSamplePadding, totalSamples / kSamplePaddingDivisor);

    std::memset(slopeRateHistogram_, 0, sizeof(slopeRateHistogram_));
  }

  /**
   * @brief Report statistics from a completed code block.
   *
   * Called by the encoder thread after a block finishes T1 encoding and
   * convex hull computation. Adds this block's pass slopes and rates to
   * the histogram, and periodically recomputes the threshold estimate.
   *
   * @param passSlopesLog Array of log-domain slopes (from convex hull).
   *                      Slope of 0 indicates a non-feasible truncation point.
   * @param passRates Array of cumulative byte counts per pass.
   * @param numPasses Number of coding passes in this block. May be zero, for a
   *                  block that coded to nothing. Such a block still counts
   *                  toward the coded sample total, and leaves the histogram
   *                  unchanged.
   * @param blockArea Number of samples in this block (width × height).
   *
   * @note Only feasible truncation points (slope != 0) are added to the
   *       histogram. Non-feasible points are interior to the convex hull
   *       and would never be selected by PCRD.
   */
  void updateStats(const uint16_t* passSlopesLog, const uint16_t* passRates, uint8_t numPasses,
                   uint32_t blockArea)
  {
    // Accumulate into local temporaries, then update under lock.
    // The histogram bins record: "total bytes belonging to passes whose
    // slope falls in bin s". When PCRD searches for threshold T, it sums
    // H[T..2047] to get total bytes that would be included at threshold T.
    uint64_t localBins[kSlopeBins] = {};
    int localMin = kSlopeBins - 1;
    int localMax = 0;

    uint16_t prevRate = 0;
    for(uint8_t p = 0; p < numPasses; p++)
    {
      uint16_t slope = passSlopesLog[p];
      uint16_t rate = passRates[p];
      uint16_t passBytes = rate - prevRate;
      prevRate = rate;

      if(slope == 0)
        continue; // Non-feasible point — skip

      // Quantize slope to histogram bin
      int bin = (slope >> kSlopeBinShift) - 2048;
      if(bin < 0)
        bin = 0;
      if(bin >= kSlopeBins)
        bin = kSlopeBins - 1;

      localBins[bin] += passBytes;
      if(bin < localMin)
        localMin = bin;
      if(bin > localMax)
        localMax = bin;
    }

    // Update shared state under lock
    {
      std::lock_guard<std::mutex> lock(mutex_);
      codedSamples_ += blockArea;

      if(localMin < minBin_)
        minBin_ = localMin;
      if(localMax > maxBin_)
        maxBin_ = localMax;

      for(int s = localMin; s <= localMax; s++)
      {
        if(localBins[s] != 0)
          slopeRateHistogram_[s] += localBins[s];
      }

      // Exponential backoff: recompute threshold less frequently as stats stabilize.
      // Initial interval is 2 blocks, doubling up to 16 blocks.
      // This bounds contention while keeping the threshold responsive early on.
      if(++updateCounter_ >= nextUpdateInterval_)
      {
        updateCounter_ = 0;
        if(nextUpdateInterval_ < 16)
          nextUpdateInterval_ *= 2;
        recomputeThreshold();
      }
    }
  }

  /**
   * @brief Get the current early-stop slope threshold, already scaled.
   *
   * Lock-free atomic read. Safe to call from any thread at any time.
   *
   * @return Log-domain slope threshold (uint16_t). Code blocks should stop
   *         encoding passes when the pass slope drops below this value.
   *         Returns 0 if insufficient data to estimate (encode all passes).
   */
  uint16_t getEarlyStopSlope() const
  {
    return currentThreshold_.load(std::memory_order_relaxed);
  }

private:
  /**
   * @brief Recompute the slope threshold from the current histogram.
   *
   * ALGORITHM:
   *
   * We simulate what PCRD-opt would do if the image were fully encoded:
   * scan from the highest slope bin downward, accumulating included bytes,
   * until the total exceeds the target. The bin where we cross the target
   * is our threshold estimate.
   *
   * The target is adjusted for partial data:
   *
   *   adjustedTarget = targetRate × (codedSamples + conservativePadding)
   *
   * The padding ensures that early in encoding (few blocks complete),
   * the adjusted target is much larger than the actual included bytes,
   * keeping the threshold low (conservative). As encoding progresses
   * and codedSamples → totalSamples, the padding becomes negligible.
   *
   * Must be called while holding mutex_.
   */
  void recomputeThreshold()
  {
    // Don't publish until we have enough data
    double fraction = static_cast<double>(codedSamples_) / static_cast<double>(totalSamples_);
    if(fraction < kMinSampleFraction)
      return;

    // Compute byte budget: how many bytes should be included at the threshold.
    // We scale by (coded + padding) / total to extrapolate from partial data.
    double adjustedSamples = static_cast<double>(codedSamples_ + conservativePadding_);
    uint64_t maxBytes = static_cast<uint64_t>(1.0 + adjustedSamples * targetRate_);

    // Scan from highest slope down, accumulating bytes
    uint64_t cumulativeBytes = 0;
    int thresholdBin = 0;
    for(int s = maxBin_; s >= minBin_; s--)
    {
      cumulativeBytes += slopeRateHistogram_[s];
      if(cumulativeBytes >= maxBytes)
      {
        thresholdBin = s;
        break;
      }
    }

    if(thresholdBin <= 0)
      return; // All data fits → no truncation needed → don't limit encoder

    // the top of the bin, PCRD's own threshold lies anywhere inside it
    int rawThreshold = ((thresholdBin + 2048) << kSlopeBinShift) + kSlopeBinWidth - 1;

    int scaleShift =
        static_cast<int>(std::lround(kLogSlopeUnitsPerOctave * std::log2(thresholdScale_)));
    constexpr int maxLogSlope = std::numeric_limits<uint16_t>::max();
    int scaledThreshold = std::clamp(rawThreshold + scaleShift, 1, maxLogSlope);

    currentThreshold_.store(static_cast<uint16_t>(scaledThreshold), std::memory_order_relaxed);
  }

  // --- Configuration (immutable after construction) ---

  uint64_t totalSamples_; ///< Total subband samples in tile
  double targetRate_; ///< Target bytes per sample
  double thresholdScale_; ///< Scale applied to the estimate before publishing
  uint64_t conservativePadding_; ///< Extra samples for extrapolating the byte budget

  // --- Mutable state (protected by mutex_) ---

  std::mutex mutex_;
  uint64_t codedSamples_; ///< Samples encoded so far
  int minBin_; ///< Lowest occupied histogram bin
  int maxBin_; ///< Highest occupied histogram bin
  uint64_t slopeRateHistogram_[kSlopeBins]; ///< Bytes per quantized slope bin

  // --- Published threshold (atomic, lock-free read) ---

  std::atomic<uint16_t> currentThreshold_;

  // --- Update scheduling ---

  int updateCounter_; ///< Blocks since last recompute
  int nextUpdateInterval_; ///< Blocks between recomputes (grows)
};

} // namespace grk
