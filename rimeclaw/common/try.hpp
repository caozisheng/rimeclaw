// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>
#include <utility>

// ─── RC_TRY / RC_TRY_ASSIGN ────────────────────────────────────────────────
//
// Propagates errors up the call stack, similar to Rust's `?` operator.
//
// Given an expression that returns a StatusOr<T> (or any type that is
// contextually convertible to bool for "is OK" and supports operator* for
// value extraction), RC_TRY either:
//   • returns the error from the enclosing function if the result is falsy, or
//   • yields the unwrapped success value if truthy.
//
// Two variants:
//
//   RC_TRY_ASSIGN(var, expr)   — Cross-platform (GCC/Clang/MSVC).
//                                 Declares `var` with the unwrapped value.
//     RC_TRY_ASSIGN(val, ComputeSomething(x));   // val is now an int
//     RC_TRY_ASSIGN(data, LoadFile(path));        // data is now Bytes
//
//   RC_TRY(expr)               — GCC/Clang only (statement expression).
//                                 Can be used inline as an expression.
//     auto val = RC_TRY(ComputeSomething(x));

namespace rimeclaw::rc_detail {

template <typename T>
struct unwrap_result {
  // Return by value (not decltype(auto)) to prevent dangling references
  // when used in GCC statement expressions where the source is destroyed.
  static auto
  get(T&& v) -> std::remove_reference_t<decltype(*std::forward<T>(v))> {
    return *std::forward<T>(v);
  }
};

}  // namespace rimeclaw::rc_detail

// ── Cross-platform: RC_TRY_ASSIGN(var, expr) ────────────────────────────────
// Works on GCC, Clang, and MSVC.  Declares `var` in the enclosing scope.
#define RC_TRY_CONCAT_IMPL(a, b) a##b
#define RC_TRY_CONCAT(a, b) RC_TRY_CONCAT_IMPL(a, b)

#define RC_TRY_ASSIGN(var, ...)                                          \
  auto RC_TRY_CONCAT(_rc_try_r_, __LINE__) = (__VA_ARGS__);              \
  if (!static_cast<bool>(RC_TRY_CONCAT(_rc_try_r_, __LINE__)))           \
    return std::forward<decltype(RC_TRY_CONCAT(_rc_try_r_, __LINE__))>(  \
        RC_TRY_CONCAT(_rc_try_r_, __LINE__));                            \
  auto var = ::rimeclaw::rc_detail::                                    \
      unwrap_result<decltype(RC_TRY_CONCAT(_rc_try_r_, __LINE__))>::get( \
          std::forward<decltype(RC_TRY_CONCAT(_rc_try_r_, __LINE__))>(   \
              RC_TRY_CONCAT(_rc_try_r_, __LINE__)))

// ── GCC/Clang only: RC_TRY(expr) ────────────────────────────────────────────
// Usable as an expression: auto val = RC_TRY(expr);
#if defined(__GNUC__) || defined(__clang__)
#define RC_TRY(...)                                            \
  __extension__({                                              \
    auto _r_ = (__VA_ARGS__);                                  \
    if (!static_cast<bool>(_r_))                               \
      return std::forward<decltype(_r_)>(_r_);                 \
    ::rimeclaw::rc_detail::unwrap_result<decltype(_r_)>::get( \
        std::forward<decltype(_r_)>(_r_));                     \
  })
#endif
