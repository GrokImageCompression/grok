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

#include "hwy/base.h"
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/arithmetic_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/nanobenchmark.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

struct TestPlusMinus {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v2 = Iota(d, T{2});
    const auto v3 = Iota(d, T{3});
    const auto v4 = Iota(d, T{4});

    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(lanes);
    for (size_t i = 0; i < N; ++i) {
      lanes[i] = static_cast<T>((2 + i) + (3 + i));
    }
    HWY_ASSERT_VEC_EQ(d, lanes.get(), Add(v2, v3));
    HWY_ASSERT_VEC_EQ(d, Set(d, T{2}), Sub(v4, v2));

    for (size_t i = 0; i < N; ++i) {
      lanes[i] = static_cast<T>((2 + i) + (4 + i));
    }
    auto sum = v2;
    sum = Add(sum, v4);  // sum == 6,8..
    HWY_ASSERT_VEC_EQ(d, Load(d, lanes.get()), sum);

    sum = Sub(sum, v4);
    HWY_ASSERT_VEC_EQ(d, v2, sum);
  }
};

struct TestPlusMinusOverflow {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v1 = Iota(d, T(1));
    const auto vMax = Iota(d, LimitsMax<T>());
    const auto vMin = Iota(d, LimitsMin<T>());

    // Check that no UB triggered.
    // "assert" here is formal - to avoid compiler dropping calculations
    HWY_ASSERT_VEC_EQ(d, Add(v1, vMax), Add(vMax, v1));
    HWY_ASSERT_VEC_EQ(d, Add(vMax, vMax), Add(vMax, vMax));
    HWY_ASSERT_VEC_EQ(d, Sub(vMin, v1), Sub(vMin, v1));
    HWY_ASSERT_VEC_EQ(d, Sub(vMin, vMax), Sub(vMin, vMax));
  }
};

HWY_NOINLINE void TestAllPlusMinus() {
  ForAllTypes(ForPartialVectors<TestPlusMinus>());
  ForIntegerTypes(ForPartialVectors<TestPlusMinusOverflow>());
}

struct TestUnsignedSaturatingArithmetic {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v0 = Zero(d);
    const auto vi = Iota(d, T{1});
    const auto vm = Set(d, LimitsMax<T>());

    HWY_ASSERT_VEC_EQ(d, Add(v0, v0), SaturatedAdd(v0, v0));
    HWY_ASSERT_VEC_EQ(d, Add(v0, vi), SaturatedAdd(v0, vi));
    HWY_ASSERT_VEC_EQ(d, Add(v0, vm), SaturatedAdd(v0, vm));
    HWY_ASSERT_VEC_EQ(d, vm, SaturatedAdd(vi, vm));
    HWY_ASSERT_VEC_EQ(d, vm, SaturatedAdd(vm, vm));

    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(v0, v0));
    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(v0, vi));
    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(vi, vi));
    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(vi, vm));
    HWY_ASSERT_VEC_EQ(d, Sub(vm, vi), SaturatedSub(vm, vi));
  }
};

struct TestSignedSaturatingArithmetic {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v0 = Zero(d);
    const auto vpm = Set(d, LimitsMax<T>());
    // Ensure all lanes are positive, even if Iota wraps around
    const auto vi = Or(And(Iota(d, 0), vpm), Set(d, T{1}));
    const auto vn = Sub(v0, vi);
    const auto vnm = Set(d, LimitsMin<T>());
    HWY_ASSERT_MASK_EQ(d, MaskTrue(d), Gt(vi, v0));
    HWY_ASSERT_MASK_EQ(d, MaskTrue(d), Lt(vn, v0));

    HWY_ASSERT_VEC_EQ(d, v0, SaturatedAdd(v0, v0));
    HWY_ASSERT_VEC_EQ(d, vi, SaturatedAdd(v0, vi));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAdd(v0, vpm));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAdd(vi, vpm));
    HWY_ASSERT_VEC_EQ(d, vpm, SaturatedAdd(vpm, vpm));

    HWY_ASSERT_VEC_EQ(d, v0, SaturatedSub(v0, v0));
    HWY_ASSERT_VEC_EQ(d, Sub(v0, vi), SaturatedSub(v0, vi));
    HWY_ASSERT_VEC_EQ(d, vn, SaturatedSub(vn, v0));
    HWY_ASSERT_VEC_EQ(d, vnm, SaturatedSub(vnm, vi));
    HWY_ASSERT_VEC_EQ(d, vnm, SaturatedSub(vnm, vpm));
  }
};

struct TestSaturatingArithmeticOverflow {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v1 = Iota(d, T(1));
    const auto vMax = Iota(d, LimitsMax<T>());
    const auto vMin = Iota(d, LimitsMin<T>());

    // Check that no UB triggered.
    // "assert" here is formal - to avoid compiler dropping calculations
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(v1, vMax), SaturatedAdd(vMax, v1));
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(vMax, vMax), SaturatedAdd(vMax, vMax));
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(vMin, vMax), SaturatedAdd(vMin, vMax));
    HWY_ASSERT_VEC_EQ(d, SaturatedAdd(vMin, vMin), SaturatedAdd(vMin, vMin));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMin, v1), SaturatedSub(vMin, v1));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMin, vMax), SaturatedSub(vMin, vMax));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMax, vMin), SaturatedSub(vMax, vMin));
    HWY_ASSERT_VEC_EQ(d, SaturatedSub(vMin, vMin), SaturatedSub(vMin, vMin));
  }
};

HWY_NOINLINE void TestAllSaturatingArithmetic() {
  ForUnsignedTypes(ForPartialVectors<TestUnsignedSaturatingArithmetic>());
  ForSignedTypes(ForPartialVectors<TestSignedSaturatingArithmetic>());
  ForIntegerTypes(ForPartialVectors<TestSaturatingArithmeticOverflow>());
}

struct TestAverage {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v0 = Zero(d);
    const auto v1 = Set(d, T{1});
    const auto v2 = Set(d, T{2});

    HWY_ASSERT_VEC_EQ(d, v0, AverageRound(v0, v0));
    HWY_ASSERT_VEC_EQ(d, v1, AverageRound(v0, v1));
    HWY_ASSERT_VEC_EQ(d, v1, AverageRound(v1, v1));
    HWY_ASSERT_VEC_EQ(d, v2, AverageRound(v1, v2));
    HWY_ASSERT_VEC_EQ(d, v2, AverageRound(v2, v2));
  }
};

HWY_NOINLINE void TestAllAverage() {
  const ForPartialVectors<TestAverage> test;
  test(uint8_t());
  test(uint16_t());
}

struct TestAbs {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v0 = Zero(d);
    const auto vp1 = Set(d, T{1});
    const auto vn1 = Set(d, T{-1});
    const auto vpm = Set(d, LimitsMax<T>());
    const auto vnm = Set(d, LimitsMin<T>());

    HWY_ASSERT_VEC_EQ(d, v0, Abs(v0));
    HWY_ASSERT_VEC_EQ(d, vp1, Abs(vp1));
    HWY_ASSERT_VEC_EQ(d, vp1, Abs(vn1));
    HWY_ASSERT_VEC_EQ(d, vpm, Abs(vpm));
    HWY_ASSERT_VEC_EQ(d, vnm, Abs(vnm));
  }
};

struct TestFloatAbs {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v0 = Zero(d);
    const auto vp1 = Set(d, T{1});
    const auto vn1 = Set(d, T{-1});
    const auto vp2 = Set(d, static_cast<T>(0.01));
    const auto vn2 = Set(d, static_cast<T>(-0.01));

    HWY_ASSERT_VEC_EQ(d, v0, Abs(v0));
    HWY_ASSERT_VEC_EQ(d, vp1, Abs(vp1));
    HWY_ASSERT_VEC_EQ(d, vp1, Abs(vn1));
    HWY_ASSERT_VEC_EQ(d, vp2, Abs(vp2));
    HWY_ASSERT_VEC_EQ(d, vp2, Abs(vn2));
  }
};

HWY_NOINLINE void TestAllAbs() {
  ForSignedTypes(ForPartialVectors<TestAbs>());
  ForFloatTypes(ForPartialVectors<TestFloatAbs>());
}

struct TestIntegerNeg {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const RebindToUnsigned<D> du;
    using TU = TFromD<decltype(du)>;
    const Vec<D> v0 = Zero(d);
    const Vec<D> v1 = BitCast(d, Set(du, TU{1}));
    const Vec<D> vp = BitCast(d, Set(du, TU{3}));
    const Vec<D> vn = Add(Not(vp), v1);  // 2's complement
    HWY_ASSERT_VEC_EQ(d, v0, Neg(v0));
    HWY_ASSERT_VEC_EQ(d, vp, Neg(vn));
    HWY_ASSERT_VEC_EQ(d, vn, Neg(vp));
  }
};

struct TestFloatNeg {
  // Must be inlined on aarch64 for bf16, else clang crashes.
  template <typename T, class D>
  HWY_INLINE void operator()(T /*unused*/, D d) {
    const RebindToUnsigned<D> du;
    using TU = TFromD<decltype(du)>;
    // 1.25 in binary16.
    const Vec<D> vp =
        BitCast(d, Set(du, static_cast<TU>(Unpredictable1() * 0x3D00)));
    // Flip sign bit in MSB
    const Vec<D> vn = BitCast(d, Xor(BitCast(du, vp), SignBit(du)));
    // Do not check negative zero - we do not yet have proper bfloat16_t Eq().
    HWY_ASSERT_VEC_EQ(du, BitCast(du, vp), BitCast(du, Neg(vn)));
    HWY_ASSERT_VEC_EQ(du, BitCast(du, vn), BitCast(du, Neg(vp)));
  }
};

struct TestNegOverflow {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto vn = Set(d, LimitsMin<T>());
    const auto vp = Set(d, LimitsMax<T>());
    HWY_ASSERT_VEC_EQ(d, Neg(vn), Neg(vn));
    HWY_ASSERT_VEC_EQ(d, Neg(vp), Neg(vp));
  }
};

HWY_NOINLINE void TestAllNeg() {
  ForFloatTypes(ForPartialVectors<TestFloatNeg>());
  // Always supported, even if !HWY_HAVE_FLOAT16.
  ForPartialVectors<TestFloatNeg>()(float16_t());

  ForSignedTypes(ForPartialVectors<TestIntegerNeg>());

  ForSignedTypes(ForPartialVectors<TestNegOverflow>());
}

struct TestIntegerAbsDiff {
  template <typename T, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))>
  static inline T ScalarAbsDiff(T a, T b) {
    using TW = MakeSigned<MakeWide<T>>;
    const TW diff = static_cast<TW>(static_cast<TW>(a) - static_cast<TW>(b));
    return static_cast<T>((diff >= 0) ? diff : -diff);
  }
  template <typename T, HWY_IF_T_SIZE(T, 8)>
  static inline T ScalarAbsDiff(T a, T b) {
    if (a >= b) {
      return static_cast<T>(static_cast<uint64_t>(a) -
                            static_cast<uint64_t>(b));
    } else {
      return static_cast<T>(static_cast<uint64_t>(b) -
                            static_cast<uint64_t>(a));
    }
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto in_lanes_a = AllocateAligned<T>(N);
    auto in_lanes_b = AllocateAligned<T>(N);
    auto out_lanes = AllocateAligned<T>(N);
    constexpr size_t shift_amt_mask = sizeof(T) * 8 - 1;
    for (size_t i = 0; i < N; ++i) {
      // Need to mask out shift_amt as i can be greater than or equal to
      // the number of bits in T if T is int8_t, uint8_t, int16_t, or uint16_t.
      const auto shift_amt = i & shift_amt_mask;
      in_lanes_a[i] =
          static_cast<T>((static_cast<uint64_t>(i) ^ 1u) << shift_amt);
      in_lanes_b[i] = static_cast<T>(static_cast<uint64_t>(i) << shift_amt);
      out_lanes[i] = ScalarAbsDiff(in_lanes_a[i], in_lanes_b[i]);
    }
    const auto a = Load(d, in_lanes_a.get());
    const auto b = Load(d, in_lanes_b.get());
    const auto expected = Load(d, out_lanes.get());
    HWY_ASSERT_VEC_EQ(d, expected, AbsDiff(a, b));
    HWY_ASSERT_VEC_EQ(d, expected, AbsDiff(b, a));
  }
};

HWY_NOINLINE void TestAllIntegerAbsDiff() {
  ForPartialVectors<TestIntegerAbsDiff>()(int8_t());
  ForPartialVectors<TestIntegerAbsDiff>()(uint8_t());
  ForPartialVectors<TestIntegerAbsDiff>()(int16_t());
  ForPartialVectors<TestIntegerAbsDiff>()(uint16_t());
  ForPartialVectors<TestIntegerAbsDiff>()(int32_t());
  ForPartialVectors<TestIntegerAbsDiff>()(uint32_t());
#if HWY_HAVE_INTEGER64
  ForPartialVectors<TestIntegerAbsDiff>()(int64_t());
  ForPartialVectors<TestIntegerAbsDiff>()(uint64_t());
#endif
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace hwy {
HWY_BEFORE_TEST(HwyArithmeticTest);
HWY_EXPORT_AND_TEST_P(HwyArithmeticTest, TestAllPlusMinus);
HWY_EXPORT_AND_TEST_P(HwyArithmeticTest, TestAllSaturatingArithmetic);
HWY_EXPORT_AND_TEST_P(HwyArithmeticTest, TestAllAverage);
HWY_EXPORT_AND_TEST_P(HwyArithmeticTest, TestAllAbs);
HWY_EXPORT_AND_TEST_P(HwyArithmeticTest, TestAllNeg);
HWY_EXPORT_AND_TEST_P(HwyArithmeticTest, TestAllIntegerAbsDiff);
}  // namespace hwy

#endif
