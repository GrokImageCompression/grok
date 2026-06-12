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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "RescaleComponent.h"

namespace
{
int g_failures = 0;

#define EXPECT(cond)                                                       \
  do                                                                       \
  {                                                                        \
    if(!(cond))                                                            \
    {                                                                      \
      ++g_failures;                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                      \
  } while(0)

#define EXPECT_EQ(a, b)                                                                          \
  do                                                                                             \
  {                                                                                              \
    auto _a = (a);                                                                               \
    auto _b = (b);                                                                               \
    if(!(_a == _b))                                                                              \
    {                                                                                            \
      ++g_failures;                                                                              \
      std::fprintf(stderr, "FAIL %s:%d: %s == %s  (got %lld vs %lld)\n", __FILE__, __LINE__, #a, \
                   #b, (long long)_a, (long long)_b);                                            \
    }                                                                                            \
  } while(0)

struct CompBuf
{
  grk_image_comp comp{};
  std::vector<int32_t> data;

  CompBuf(uint32_t w, uint32_t h, std::initializer_list<int32_t> init)
  {
    comp.w = w;
    comp.h = h;
    comp.stride = w;
    comp.prec = 16;
    comp.sgnd = false;
    data.assign(init);
    comp.data = data.data();
  }
};

void test_basic_forward_mapping()
{
  // 1000..5000 -> 0..255, mid-range pixels.
  CompBuf c(4, 1, {1000, 3000, 5000, 9000});
  grk_rescale r{1000.0, 5000.0, 0.0, 255.0};
  EXPECT(grk::rescale_component<int32_t>(&c.comp, r));
  EXPECT_EQ(c.data[0], 0); // src_min -> dst_min
  EXPECT_EQ(c.data[1], 128); // midpoint: llround(127.5) = 128 (half away from zero)
  EXPECT_EQ(c.data[2], 255); // src_max -> dst_max
  EXPECT_EQ(c.data[3], 255); // above src_max -> clamped to dst_max
  EXPECT_EQ((int)c.comp.prec, 8);
  EXPECT_EQ((int)c.comp.sgnd, 0);
}

void test_clamp_below_src_min()
{
  CompBuf c(2, 1, {-500, 0});
  grk_rescale r{1000.0, 5000.0, 0.0, 255.0};
  EXPECT(grk::rescale_component<int32_t>(&c.comp, r));
  EXPECT_EQ(c.data[0], 0);
  EXPECT_EQ(c.data[1], 0);
}

void test_reversed_dst_range()
{
  // Reversed dst: maps high source values to low output values (negative slope).
  CompBuf c(3, 1, {0, 50, 100});
  grk_rescale r{0.0, 100.0, 255.0, 0.0};
  EXPECT(grk::rescale_component<int32_t>(&c.comp, r));
  EXPECT_EQ(c.data[0], 255);
  EXPECT_EQ(c.data[1], 128); // 255 - 127.5 → llround(127.5) = 128
  EXPECT_EQ(c.data[2], 0);
}

void test_signed_dst_range()
{
  // Signed dst should flip comp.sgnd and account for sign bit in prec.
  CompBuf c(3, 1, {0, 500, 1000});
  grk_rescale r{0.0, 1000.0, -128.0, 127.0};
  EXPECT(grk::rescale_component<int32_t>(&c.comp, r));
  EXPECT_EQ(c.data[0], -128);
  EXPECT_EQ(c.data[2], 127);
  EXPECT_EQ((int)c.comp.sgnd, 1);
  EXPECT_EQ((int)c.comp.prec, 8); // [-128, 127] fits exactly in 8-bit signed
}

void test_stride_padding_ignored()
{
  // stride > w: the column past w must not be touched.
  grk_image_comp comp{};
  comp.w = 2;
  comp.h = 2;
  comp.stride = 4;
  comp.prec = 16;
  comp.sgnd = false;
  // Layout (stride=4): [a,b,X,X, c,d,X,X]
  std::vector<int32_t> buf = {1000, 5000, 7777, 7777, 1000, 5000, 7777, 7777};
  comp.data = buf.data();
  grk_rescale r{1000.0, 5000.0, 0.0, 255.0};
  EXPECT(grk::rescale_component<int32_t>(&comp, r));
  EXPECT_EQ(buf[0], 0);
  EXPECT_EQ(buf[1], 255);
  EXPECT_EQ(buf[2], 7777); // padding untouched
  EXPECT_EQ(buf[3], 7777);
  EXPECT_EQ(buf[4], 0);
  EXPECT_EQ(buf[5], 255);
  EXPECT_EQ(buf[6], 7777);
  EXPECT_EQ(buf[7], 7777);
}

void test_degenerate_src_range_rejected()
{
  CompBuf c(1, 1, {42});
  grk_rescale r{100.0, 100.0, 0.0, 255.0};
  EXPECT(!grk::rescale_component<int32_t>(&c.comp, r));
  EXPECT_EQ(c.data[0], 42); // untouched
}

void test_identity_rescale_preserves_values()
{
  CompBuf c(3, 1, {10, 20, 30});
  grk_rescale r{0.0, 255.0, 0.0, 255.0};
  EXPECT(grk::rescale_component<int32_t>(&c.comp, r));
  EXPECT_EQ(c.data[0], 10);
  EXPECT_EQ(c.data[1], 20);
  EXPECT_EQ(c.data[2], 30);
}

} // namespace

int main()
{
  test_basic_forward_mapping();
  test_clamp_below_src_min();
  test_reversed_dst_range();
  test_signed_dst_range();
  test_stride_padding_ignored();
  test_degenerate_src_range_rejected();
  test_identity_rescale_preserves_values();

  if(g_failures == 0)
  {
    std::fprintf(stderr, "GrkRescaleTest: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "GrkRescaleTest: %d failure(s)\n", g_failures);
  return 1;
}
