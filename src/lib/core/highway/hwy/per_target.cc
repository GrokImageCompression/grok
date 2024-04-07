// Copyright 2022 Google LLC
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

#include "hwy/per_target.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/per_target.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
size_t GetVectorBytes() { return Lanes(ScalableTag<uint8_t>()); }
bool GetHaveFloat16() { return HWY_HAVE_FLOAT16 != 0; }
bool GetHaveFloat64() { return HWY_HAVE_FLOAT64 != 0; }
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE

}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_EXPORT(GetVectorBytes);
HWY_EXPORT(GetHaveFloat16);
HWY_EXPORT(GetHaveFloat64);
}  // namespace

HWY_DLLEXPORT size_t VectorBytes() {
  return HWY_DYNAMIC_DISPATCH(GetVectorBytes)();
}

HWY_DLLEXPORT bool HaveFloat16() {
  return HWY_DYNAMIC_DISPATCH(GetHaveFloat16)();
}

HWY_DLLEXPORT bool HaveFloat64() {
  return HWY_DYNAMIC_DISPATCH(GetHaveFloat64)();
}

}  // namespace hwy
#endif  // HWY_ONCE
