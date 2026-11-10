/*
 * Copyright 2026 Duatic AG
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The EtherCAT Technology, and the trade name "EtherCAT" are the intellectual property of, and protected by Beckhoff
 * Automation GmbH. You may review, edit, compile, link, debug the EtherCAT Master Code, provided that before
 * implementing into a product, using, selling or otherwise distributing an EtherCAT Master License is obtained from
 * Beckhoff Automation GmbH & Co.KG.
 *
 * To receive a copy of the EtherCAT Master License contact Beckhoff Automation GmbH & Co.KG, Huelshorstweg 20,
 * D-33415 Verl, Germany (www.beckhoff.com).
 */

#pragma once

#include <optional>
#include <utility>
#include <functional>
#include <type_traits>
#include <algorithm>

namespace duatic::ecat_interface
{

/**
 * ValueDiagnosticsWrapper - a wrapper around a read value<T> together with additional diagnostics data
 * @note even if the read was successful there might not necessarily be a value inside
 */
template <typename T, typename DiagnosticsT>
class [[nodiscard]] ValueDiagnosticsWrapper
{
public:
  using value_type = T;
  using diagnostics_type = DiagnosticsT;

  static_assert(std::is_nothrow_move_constructible_v<DiagnosticsT>);

  ValueDiagnosticsWrapper() = default;

  explicit ValueDiagnosticsWrapper(DiagnosticsT diagnostics) : diagnostics_{ std::move(diagnostics) }
  {
  }

  ValueDiagnosticsWrapper(DiagnosticsT diagnostics, T value)
    : diagnostics_{ std::move(diagnostics) }, value_{ std::move(value) }
  {
  }

  [[nodiscard]] bool has_value() const noexcept
  {
    return value_.has_value();
  }

  [[nodiscard]] explicit operator bool() const noexcept requires(!std::is_same_v<std::remove_cv_t<T>, bool>)
  {
    return value_.has_value();
  }

  const T& operator*() const& noexcept
  {
    return *value_;
  }

  T& operator*() & noexcept
  {
    return *value_;
  }

  T&& operator*() && noexcept
  {
    return *std::move(value_);
  }

  const T* operator->() const noexcept
  {
    return value_.operator->();
  }

  T* operator->() noexcept
  {
    return value_.operator->();
  }

  const T& value() const&
  {
    return value_.value();
  }

  T& value() &
  {
    return value_.value();
  }

  T&& value() &&
  {
    return std::move(value_).value();
  }

  template <typename U>
  [[nodiscard]] T value_or(U&& fallback) const&
  {
    return value_.value_or(std::forward<U>(fallback));
  }

  template <typename U>
  [[nodiscard]] T value_or(U&& fallback) &&
  {
    return std::move(value_).value_or(std::forward<U>(fallback));
  }

  [[nodiscard]] const DiagnosticsT& diagnostics() const& noexcept
  {
    return diagnostics_;
  }

  [[nodiscard]] DiagnosticsT diagnostics() && noexcept
  {
    return std::move(diagnostics_);
  }

  [[nodiscard]] const std::optional<T>& as_optional() const& noexcept
  {
    return value_;
  }

  [[nodiscard]] std::optional<T> as_optional() &&
  {
    return std::move(value_);
  }

  template <typename F>
  [[nodiscard]] auto transform(F&& func) const&
  {
    using U = std::remove_cvref_t<std::invoke_result_t<F, const T&>>;
    static_assert(!std::is_void_v<U>, "transform requires a value-returning function");
    if (!value_.has_value()) {
      return ValueDiagnosticsWrapper<U, DiagnosticsT>{ diagnostics_ };
    }
    return ValueDiagnosticsWrapper<U, DiagnosticsT>{ diagnostics_, std::invoke(std::forward<F>(func), *value_) };
  }

  template <typename F>
  [[nodiscard]] auto transform(F&& func) &&
  {
    using U = std::remove_cvref_t<std::invoke_result_t<F, T&&>>;
    static_assert(!std::is_void_v<U>, "transform requires a value-returning function");
    if (!value_.has_value()) {
      return ValueDiagnosticsWrapper<U, DiagnosticsT>{ std::move(diagnostics_) };
    }
    return ValueDiagnosticsWrapper<U, DiagnosticsT>{ std::move(diagnostics_),
                                                     std::invoke(std::forward<F>(func), *std::move(value_)) };
  }

private:
  DiagnosticsT diagnostics_{};
  std::optional<T> value_{};
};

}  // namespace duatic::ecat_interface
