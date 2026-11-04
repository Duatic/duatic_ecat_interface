
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
 */
#include <gtest/gtest.h>

#include <string>

#include "duatic_ecat_interface/value_diagnostics_wrapper.hpp"

using namespace duatic::ecat_interface;  // NOLINT(build/namespaces)

namespace
{
// A minimal, trivially/nothrow-move-constructible diagnostics payload,
// analogous to SDOReadResult/SDOWriteResult but without pulling in types.hpp.
struct MockDiagnostics
{
  bool success{ false };
  int code{ 0 };
};

using Wrapper = ValueDiagnosticsWrapper<int, MockDiagnostics>;
}  // namespace

TEST(ValueDiagnosticsWrapperTest, DefaultConstructedHasNoValue)
{
  Wrapper w;
  EXPECT_FALSE(w.has_value());
  EXPECT_FALSE(static_cast<bool>(w));
  EXPECT_FALSE(w.diagnostics().success);
}

TEST(ValueDiagnosticsWrapperTest, DiagnosticsOnlyConstructorHasNoValue)
{
  Wrapper w{ MockDiagnostics{ true, 42 } };

  EXPECT_FALSE(w.has_value());
  EXPECT_FALSE(static_cast<bool>(w));
  EXPECT_EQ(w.diagnostics().code, 42);
  EXPECT_TRUE(w.diagnostics().success);
}

TEST(ValueDiagnosticsWrapperTest, DiagnosticsAndValueConstructorHasValue)
{
  Wrapper w{ MockDiagnostics{ true, 7 }, 123 };

  ASSERT_TRUE(w.has_value());
  EXPECT_TRUE(static_cast<bool>(w));
  EXPECT_EQ(*w, 123);
  EXPECT_EQ(w.value(), 123);
  EXPECT_EQ(w.diagnostics().code, 7);
}

TEST(ValueDiagnosticsWrapperTest, ValueThrowsWhenEmpty)
{
  Wrapper w{ MockDiagnostics{} };
  EXPECT_THROW(w.value(), std::bad_optional_access);
}

TEST(ValueDiagnosticsWrapperTest, ValueOrReturnsFallbackWhenEmpty)
{
  Wrapper w{ MockDiagnostics{} };
  EXPECT_EQ(w.value_or(-1), -1);
}

TEST(ValueDiagnosticsWrapperTest, ValueOrReturnsValueWhenPresent)
{
  Wrapper w{ MockDiagnostics{}, 99 };
  EXPECT_EQ(w.value_or(-1), 99);
}

TEST(ValueDiagnosticsWrapperTest, AsOptionalReflectsState)
{
  Wrapper empty{ MockDiagnostics{} };
  Wrapper filled{ MockDiagnostics{}, 5 };

  EXPECT_FALSE(empty.as_optional().has_value());
  ASSERT_TRUE(filled.as_optional().has_value());
  EXPECT_EQ(*filled.as_optional(), 5);
}

TEST(ValueDiagnosticsWrapperTest, TransformAppliesFunctionWhenValuePresent)
{
  Wrapper w{ MockDiagnostics{ true, 1 }, 10 };

  auto transformed = w.transform([](const int& v) { return std::to_string(v * 2); });

  ASSERT_TRUE(transformed.has_value());
  EXPECT_EQ(*transformed, "20");
  EXPECT_EQ(transformed.diagnostics().code, 1);
}

TEST(ValueDiagnosticsWrapperTest, TransformPropagatesDiagnosticsWhenEmpty)
{
  Wrapper w{ MockDiagnostics{ false, 3 } };

  auto transformed = w.transform([](const int& v) { return std::to_string(v); });

  EXPECT_FALSE(transformed.has_value());
  EXPECT_EQ(transformed.diagnostics().code, 3);
}

TEST(ValueDiagnosticsWrapperTest, TransformOnRvalueMovesThrough)
{
  Wrapper w{ MockDiagnostics{ true, 2 }, 4 };

  auto transformed = std::move(w).transform([](int&& v) { return v + 1; });

  ASSERT_TRUE(transformed.has_value());
  EXPECT_EQ(*transformed, 5);
}
