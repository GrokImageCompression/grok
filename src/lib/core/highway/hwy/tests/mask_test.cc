// Copyright 2019 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>  // memcmp

#include <algorithm>  // std::fill

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/mask_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// All types.
struct TestFromVec {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(lanes);

    memset(lanes.get(), 0, N * sizeof(T));
    const auto actual_false = MaskFromVec(Load(d, lanes.get()));
    HWY_ASSERT_MASK_EQ(d, MaskFalse(d), actual_false);

    memset(lanes.get(), 0xFF, N * sizeof(T));
    const auto actual_true = MaskFromVec(Load(d, lanes.get()));
    HWY_ASSERT_MASK_EQ(d, MaskTrue(d), actual_true);
  }
};

HWY_NOINLINE void TestAllFromVec() {
  ForAllTypes(ForPartialVectors<TestFromVec>());
}

struct TestFirstN {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes);

    using TN = SignedFromSize<HWY_MIN(sizeof(size_t), sizeof(T))>;
    const size_t max_len = static_cast<size_t>(LimitsMax<TN>());

    const size_t max_lanes = HWY_MIN(2 * N, AdjustedReps(512));
    for (size_t len = 0; len <= HWY_MIN(max_lanes, max_len); ++len) {
      // Loop instead of Iota+Lt to avoid wraparound for 8-bit T.
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (i < len) ? T{1} : T{0};
      }
      const auto expected = Eq(Load(d, bool_lanes.get()), Set(d, T{1}));
      HWY_ASSERT_MASK_EQ(d, expected, FirstN(d, len));
    }

    // Also ensure huge values yield all-true (unless the vector is actually
    // larger than max_len).
    for (size_t i = 0; i < N; ++i) {
      bool_lanes[i] = (i < max_len) ? T{1} : T{0};
    }
    const auto expected = Eq(Load(d, bool_lanes.get()), Set(d, T{1}));
    HWY_ASSERT_MASK_EQ(d, expected, FirstN(d, max_len));
  }
};

HWY_NOINLINE void TestAllFirstN() {
  ForAllTypes(ForPartialVectors<TestFirstN>());
}

struct TestMaskVec {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
      }

      const auto mask = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      HWY_ASSERT_MASK_EQ(d, mask, MaskFromVec(VecFromMask(d, mask)));
    }
  }
};

HWY_NOINLINE void TestAllMaskVec() {
  const ForPartialVectors<TestMaskVec> test;

  test(uint16_t());
  test(int16_t());
  // TODO(janwas): float16_t - cannot compare yet

  ForUIF3264(test);
}

struct TestAllTrueFalse {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto zero = Zero(d);
    auto v = zero;

    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(lanes);
    std::fill(lanes.get(), lanes.get() + N, T(0));

    HWY_ASSERT(AllTrue(d, Eq(v, zero)));
    HWY_ASSERT(!AllFalse(d, Eq(v, zero)));

    // Single lane implies AllFalse = !AllTrue. Otherwise, there are multiple
    // lanes and one is nonzero.
    const bool expected_all_false = (N != 1);

    // Set each lane to nonzero and back to zero
    for (size_t i = 0; i < N; ++i) {
      lanes[i] = T(1);
      v = Load(d, lanes.get());

      HWY_ASSERT(!AllTrue(d, Eq(v, zero)));

      HWY_ASSERT(expected_all_false ^ AllFalse(d, Eq(v, zero)));

      lanes[i] = T(-1);
      v = Load(d, lanes.get());
      HWY_ASSERT(!AllTrue(d, Eq(v, zero)));
      HWY_ASSERT(expected_all_false ^ AllFalse(d, Eq(v, zero)));

      // Reset to all zero
      lanes[i] = T(0);
      v = Load(d, lanes.get());
      HWY_ASSERT(AllTrue(d, Eq(v, zero)));
      HWY_ASSERT(!AllFalse(d, Eq(v, zero)));
    }
  }
};

HWY_NOINLINE void TestAllAllTrueFalse() {
  ForAllTypes(ForPartialVectors<TestAllTrueFalse>());
}

struct TestCountTrue {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = HWY_MIN(N, size_t(10));

    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      // Number of zeros written = number of mask lanes that are true.
      size_t expected = 0;
      for (size_t i = 0; i < max_lanes; ++i) {
        const bool is_true = (code & (1ull << i)) != 0;
        bool_lanes[i] = is_true ? TI(1) : TI(0);
        expected += is_true;
      }

      const auto mask = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      const size_t actual = CountTrue(d, mask);
      HWY_ASSERT_EQ(expected, actual);
    }
  }
};

HWY_NOINLINE void TestAllCountTrue() {
  ForAllTypes(ForPartialVectors<TestCountTrue>());
}

struct TestFindFirstTrue {  // Also FindKnownFirstTrue
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(9)));

    HWY_ASSERT_EQ(intptr_t(-1), FindFirstTrue(d, MaskFalse(d)));
    HWY_ASSERT_EQ(intptr_t(0), FindFirstTrue(d, MaskTrue(d)));
    HWY_ASSERT_EQ(size_t(0), FindKnownFirstTrue(d, MaskTrue(d)));

    for (size_t code = 1; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const size_t expected =
          Num0BitsBelowLS1Bit_Nonzero32(static_cast<uint32_t>(code));
      const auto mask = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      HWY_ASSERT_EQ(static_cast<intptr_t>(expected), FindFirstTrue(d, mask));
      HWY_ASSERT_EQ(expected, FindKnownFirstTrue(d, mask));
    }
  }
};

HWY_NOINLINE void TestAllFindFirstTrue() {
  ForAllTypes(ForPartialVectors<TestFindFirstTrue>());
}

struct TestFindLastTrue {  // Also FindKnownLastTrue
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(9)));

    HWY_ASSERT_EQ(intptr_t(-1), FindLastTrue(d, MaskFalse(d)));
    HWY_ASSERT_EQ(intptr_t(Lanes(d) - 1), FindLastTrue(d, MaskTrue(d)));
    HWY_ASSERT_EQ(size_t(Lanes(d) - 1), FindKnownLastTrue(d, MaskTrue(d)));

    for (size_t code = 1; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const size_t expected =
          31 - Num0BitsAboveMS1Bit_Nonzero32(static_cast<uint32_t>(code));
      const auto mask = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      HWY_ASSERT_EQ(static_cast<intptr_t>(expected), FindLastTrue(d, mask));
      HWY_ASSERT_EQ(expected, FindKnownLastTrue(d, mask));
    }
  }
};

HWY_NOINLINE void TestAllFindLastTrue() {
  ForAllTypes(ForPartialVectors<TestFindLastTrue>());
}

struct TestLogicalMask {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto m0 = MaskFalse(d);
    const auto m_all = MaskTrue(d);

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));

    HWY_ASSERT_MASK_EQ(d, m0, Not(m_all));
    HWY_ASSERT_MASK_EQ(d, m_all, Not(m0));

    HWY_ASSERT_MASK_EQ(d, m_all, ExclusiveNeither(m0, m0));
    HWY_ASSERT_MASK_EQ(d, m0, ExclusiveNeither(m_all, m0));
    HWY_ASSERT_MASK_EQ(d, m0, ExclusiveNeither(m0, m_all));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const auto m = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));

      HWY_ASSERT_MASK_EQ(d, m0, Xor(m, m));
      HWY_ASSERT_MASK_EQ(d, m0, AndNot(m, m));
      HWY_ASSERT_MASK_EQ(d, m0, AndNot(m_all, m));

      HWY_ASSERT_MASK_EQ(d, m, Or(m, m));
      HWY_ASSERT_MASK_EQ(d, m, Or(m0, m));
      HWY_ASSERT_MASK_EQ(d, m, Or(m, m0));
      HWY_ASSERT_MASK_EQ(d, m, Xor(m0, m));
      HWY_ASSERT_MASK_EQ(d, m, Xor(m, m0));
      HWY_ASSERT_MASK_EQ(d, m, And(m, m));
      HWY_ASSERT_MASK_EQ(d, m, And(m_all, m));
      HWY_ASSERT_MASK_EQ(d, m, And(m, m_all));
      HWY_ASSERT_MASK_EQ(d, m, AndNot(m0, m));
    }
  }
};

HWY_NOINLINE void TestAllLogicalMask() {
  ForAllTypes(ForPartialVectors<TestLogicalMask>());
}

struct TestSetBeforeFirst {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const auto m = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));

      const size_t first_set_lane_idx =
          (code != 0)
              ? Num0BitsBelowLS1Bit_Nonzero64(static_cast<uint64_t>(code))
              : N;
      const auto expected_mask = FirstN(d, first_set_lane_idx);

      HWY_ASSERT_MASK_EQ(d, expected_mask, SetBeforeFirst(m));
    }
  }
};

HWY_NOINLINE void TestAllSetBeforeFirst() {
  ForAllTypes(ForPartialVectors<TestSetBeforeFirst>());
}

struct TestSetAtOrBeforeFirst {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const auto m = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));

      const size_t idx_after_first_set_lane =
          (code != 0)
              ? (Num0BitsBelowLS1Bit_Nonzero64(static_cast<uint64_t>(code)) + 1)
              : N;
      const auto expected_mask = FirstN(d, idx_after_first_set_lane);

      HWY_ASSERT_MASK_EQ(d, expected_mask, SetAtOrBeforeFirst(m));
    }
  }
};

HWY_NOINLINE void TestAllSetAtOrBeforeFirst() {
  ForAllTypes(ForPartialVectors<TestSetAtOrBeforeFirst>());
}

struct TestSetOnlyFirst {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));
    auto expected_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(expected_lanes);

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      memset(expected_lanes.get(), 0, N * sizeof(TI));
      if (code != 0) {
        const size_t idx_of_first_lane =
            Num0BitsBelowLS1Bit_Nonzero64(static_cast<uint64_t>(code));
        expected_lanes[idx_of_first_lane] = TI(1);
      }

      const auto m = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      const auto expected_mask =
          RebindMask(d, Gt(Load(di, expected_lanes.get()), Zero(di)));

      HWY_ASSERT_MASK_EQ(d, expected_mask, SetOnlyFirst(m));
    }
  }
};

HWY_NOINLINE void TestAllSetOnlyFirst() {
  ForAllTypes(ForPartialVectors<TestSetOnlyFirst>());
}

struct TestSetAtOrAfterFirst {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    auto bool_lanes = AllocateAligned<TI>(N);
    HWY_ASSERT(bool_lanes);
    memset(bool_lanes.get(), 0, N * sizeof(TI));

    // For all combinations of zero/nonzero state of subset of lanes:
    const size_t max_lanes = AdjustedLog2Reps(HWY_MIN(N, size_t(6)));
    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < max_lanes; ++i) {
        bool_lanes[i] = (code & (1ull << i)) ? TI(1) : TI(0);
      }

      const auto m = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));

      const size_t first_set_lane_idx =
          (code != 0)
              ? Num0BitsBelowLS1Bit_Nonzero64(static_cast<uint64_t>(code))
              : N;
      const auto expected_at_or_after_first_mask =
          Not(FirstN(d, first_set_lane_idx));
      const auto actual_at_or_after_first_mask = SetAtOrAfterFirst(m);

      HWY_ASSERT_MASK_EQ(d, expected_at_or_after_first_mask,
                         actual_at_or_after_first_mask);
      HWY_ASSERT_MASK_EQ(
          d, SetOnlyFirst(m),
          And(actual_at_or_after_first_mask, SetAtOrBeforeFirst(m)));
      HWY_ASSERT_MASK_EQ(d, m, And(m, actual_at_or_after_first_mask));
      HWY_ASSERT(
          AllTrue(d, Xor(actual_at_or_after_first_mask, SetBeforeFirst(m))));
    }
  }
};

HWY_NOINLINE void TestAllSetAtOrAfterFirst() {
  ForAllTypes(ForPartialVectors<TestSetAtOrAfterFirst>());
}

struct TestDup128MaskFromMaskBits {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    constexpr size_t kLanesPer16ByteBlock = 16 / sizeof(T);

    auto expected = AllocateAligned<TI>(N);
    HWY_ASSERT(expected);

    // For all combinations of zero/nonzero state of subset of lanes:
    constexpr size_t kMaxLanesToCheckPerBlk =
        HWY_MIN(HWY_MAX_LANES_D(D), HWY_MIN(kLanesPer16ByteBlock, 10));
    const size_t max_lanes = HWY_MIN(N, kMaxLanesToCheckPerBlk);

    for (size_t code = 0; code < (1ull << max_lanes); ++code) {
      for (size_t i = 0; i < N; i++) {
        expected[i] = static_cast<TI>(
            -static_cast<TI>((code >> (i & (kLanesPer16ByteBlock - 1))) & 1));
      }

      const auto expected_mask =
          MaskFromVec(BitCast(d, LoadDup128(di, expected.get())));

      const auto m = Dup128MaskFromMaskBits(d, static_cast<unsigned>(code));
      HWY_ASSERT_VEC_EQ(di, expected.get(), VecFromMask(di, RebindMask(di, m)));
      HWY_ASSERT_MASK_EQ(d, expected_mask, m);
    }
  }
};

HWY_NOINLINE void TestAllDup128MaskFromMaskBits() {
  ForAllTypes(ForPartialVectors<TestDup128MaskFromMaskBits>());
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace hwy {
HWY_BEFORE_TEST(HwyMaskTest);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllFromVec);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllFirstN);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllMaskVec);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllAllTrueFalse);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllCountTrue);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllFindFirstTrue);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllFindLastTrue);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllLogicalMask);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllSetBeforeFirst);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllSetAtOrBeforeFirst);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllSetOnlyFirst);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllSetAtOrAfterFirst);
HWY_EXPORT_AND_TEST_P(HwyMaskTest, TestAllDup128MaskFromMaskBits);
}  // namespace hwy

#endif
