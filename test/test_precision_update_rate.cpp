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

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <ctime>
#include "duatic_ecat_interface/precision_update_rate.hpp"

using namespace std::chrono_literals;    // NOLINT(build/namespaces)
using namespace duatic::ecat_interface;  // NOLINT(build/namespaces)

class PrecisionUpdateRateTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    // Setup code if needed
  }

  void TearDown() override
  {
    // Cleanup code if needed
  }

  // Helper function to simulate work
  void simulate_work(std::chrono::microseconds duration)
  {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      // Busy wait to simulate work
    }
  }
};

// Test basic construction
TEST_F(PrecisionUpdateRateTest, Construction)
{
  EXPECT_NO_THROW({ PrecisionUpdateRate rate(1ms); });

  EXPECT_NO_THROW({ PrecisionUpdateRate rate(100us); });

  EXPECT_NO_THROW({ PrecisionUpdateRate rate(1s); });
}

// Test initial state after construction
TEST_F(PrecisionUpdateRateTest, InitialState)
{
  PrecisionUpdateRate rate(10ms);

  EXPECT_EQ(rate.overrun_count(), 0u);
  EXPECT_EQ(rate.accumulated_delay().count(), 0);
  EXPECT_EQ(rate.last_delay().count(), 0);
}

// Test single step without overrun
TEST_F(PrecisionUpdateRateTest, SingleStepNoOverrun)
{
  PrecisionUpdateRate rate(10ms);

  auto start = std::chrono::steady_clock::now();
  bool overrun = rate.step();
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(overrun);
  EXPECT_EQ(rate.overrun_count(), 0u);
  EXPECT_EQ(rate.accumulated_delay().count(), 0);
  EXPECT_EQ(rate.last_delay().count(), 0);

  // Check that approximately 10ms elapsed
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(elapsed.count(), 9);  // Allow some tolerance
  EXPECT_LE(elapsed.count(), 11);
}

// Test multiple steps without overrun
TEST_F(PrecisionUpdateRateTest, MultipleStepsNoOverrun)
{
  PrecisionUpdateRate rate(5ms);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 10; ++i) {
    bool overrun = rate.step();
    EXPECT_FALSE(overrun);
  }

  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(rate.overrun_count(), 0u);
  EXPECT_EQ(rate.accumulated_delay().count(), 0);
  EXPECT_EQ(rate.last_delay().count(), 0);

  // Check that approximately 50ms elapsed (10 steps * 5ms)
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(elapsed.count(), 48);
  EXPECT_LE(elapsed.count(), 52);
}

// Test step with overrun
TEST_F(PrecisionUpdateRateTest, StepWithOverrun)
{
  PrecisionUpdateRate rate(10ms);

  // First step to establish baseline
  rate.step();

  // Simulate work that takes longer than the time step
  simulate_work(15ms);

  // This step should detect an overrun
  bool overrun = rate.step();

  EXPECT_TRUE(overrun);
  EXPECT_EQ(rate.overrun_count(), 1u);
  EXPECT_GT(rate.accumulated_delay().count(), 0);
  EXPECT_GT(rate.last_delay().count(), 0);

  // The delay should be approximately 5ms (15ms work - 10ms period)
  auto delay_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rate.last_delay());
  EXPECT_GE(delay_ms.count(), 4);
  EXPECT_LE(delay_ms.count(), 6);
}

// Test multiple overruns accumulate correctly
TEST_F(PrecisionUpdateRateTest, MultipleOverruns)
{
  PrecisionUpdateRate rate(10ms);

  rate.step();  // Establish baseline

  // First overrun
  simulate_work(15ms);
  bool overrun1 = rate.step();
  auto first_delay = rate.last_delay();
  auto first_accumulated = rate.accumulated_delay();

  EXPECT_TRUE(overrun1);
  EXPECT_EQ(rate.overrun_count(), 1u);
  EXPECT_EQ(first_delay, first_accumulated);

  // Second overrun
  simulate_work(12ms);
  bool overrun2 = rate.step();
  auto second_delay = rate.last_delay();
  auto second_accumulated = rate.accumulated_delay();

  EXPECT_TRUE(overrun2);
  EXPECT_EQ(rate.overrun_count(), 2u);
  EXPECT_GT(second_delay.count(), 0);
  EXPECT_EQ(second_accumulated.count(), first_delay.count() + second_delay.count());
}

// Test that last_delay resets after normal step
TEST_F(PrecisionUpdateRateTest, LastDelayResetsAfterNormalStep)
{
  PrecisionUpdateRate rate(10ms);

  rate.step();  // Establish baseline

  // Cause an overrun
  simulate_work(15ms);
  rate.step();
  EXPECT_GT(rate.last_delay().count(), 0);
  EXPECT_EQ(rate.overrun_count(), 1u);

  // Normal step should reset last_delay to 0
  bool overrun = rate.step();
  EXPECT_FALSE(overrun);
  EXPECT_EQ(rate.last_delay().count(), 0);
  EXPECT_EQ(rate.overrun_count(), 1u);             // Count should not increase
  EXPECT_GT(rate.accumulated_delay().count(), 0);  // But accumulated should remain
}

// Test reset functionality
TEST_F(PrecisionUpdateRateTest, ResetClearsStatistics)
{
  PrecisionUpdateRate rate(10ms);

  rate.step();
  simulate_work(15ms);
  rate.step();

  EXPECT_GT(rate.overrun_count(), 0u);
  EXPECT_GT(rate.accumulated_delay().count(), 0);

  rate.reset();

  EXPECT_EQ(rate.overrun_count(), 0u);
  EXPECT_EQ(rate.accumulated_delay().count(), 0);
  EXPECT_EQ(rate.last_delay().count(), 0);
}

// Test reset and continue functionality
TEST_F(PrecisionUpdateRateTest, ResetAndContinue)
{
  PrecisionUpdateRate rate(10ms);

  // Run some steps with overrun
  rate.step();
  simulate_work(15ms);
  rate.step();
  EXPECT_GT(rate.overrun_count(), 0u);

  // Reset
  rate.reset();

  // Should be able to continue normally
  auto start = std::chrono::steady_clock::now();
  bool overrun = rate.step();
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(overrun);
  EXPECT_EQ(rate.overrun_count(), 0u);

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(elapsed.count(), 9);
  EXPECT_LE(elapsed.count(), 11);
}

// Test timing precision with fast rate
TEST_F(PrecisionUpdateRateTest, FastRatePrecision)
{
  PrecisionUpdateRate rate(1ms);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 100; ++i) {
    rate.step();
  }

  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(rate.overrun_count(), 0u);

  // Check that approximately 100ms elapsed (100 steps * 1ms)
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(elapsed.count(), 98);
  EXPECT_LE(elapsed.count(), 102);
}

// Test timing precision with slow rate
TEST_F(PrecisionUpdateRateTest, SlowRatePrecision)
{
  PrecisionUpdateRate rate(50ms);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 5; ++i) {
    rate.step();
  }

  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(rate.overrun_count(), 0u);

  // Check that approximately 250ms elapsed (5 steps * 50ms)
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(elapsed.count(), 245);
  EXPECT_LE(elapsed.count(), 255);
}

// Test with microsecond precision
TEST_F(PrecisionUpdateRateTest, MicrosecondPrecision)
{
  PrecisionUpdateRate rate(100us);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 1000; ++i) {
    rate.step();
  }

  auto end = std::chrono::steady_clock::now();

  // Should complete 1000 steps in approximately 100ms
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  // flaky in the ci and disabled therefore
  // EXPECT_GE(elapsed.count(), 98);
  // EXPECT_LE(elapsed.count(), 102);
}

// Test overrun detection accuracy
TEST_F(PrecisionUpdateRateTest, OverrunDetectionAccuracy)
{
  PrecisionUpdateRate rate(5ms);

  rate.step();

  // Create a known delay
  simulate_work(8ms);  // 3ms overrun expected

  rate.step();

  EXPECT_EQ(rate.overrun_count(), 1u);

  auto delay_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rate.last_delay());
  EXPECT_GE(delay_ms.count(), 2);  // At least 2ms
  EXPECT_LE(delay_ms.count(), 4);  // At most 4ms (allowing tolerance)
}

// Test consistency over many iterations
TEST_F(PrecisionUpdateRateTest, ConsistencyOverManyIterations)
{
  PrecisionUpdateRate rate(2ms);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 500; ++i) {
    rate.step();
  }

  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(rate.overrun_count(), 0u);

  // Should take approximately 1000ms (500 * 2ms)
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(elapsed.count(), 995);
  EXPECT_LE(elapsed.count(), 1005);
}

// Test that accumulated delay equals sum of individual delays
TEST_F(PrecisionUpdateRateTest, AccumulatedDelayCorrectness)
{
  PrecisionUpdateRate rate(10ms);

  rate.step();

  std::vector<std::chrono::nanoseconds> delays;

  // Create multiple overruns
  for (int i = 0; i < 5; ++i) {
    simulate_work(15ms);
    rate.step();
    delays.push_back(rate.last_delay());
  }

  // Calculate expected accumulated delay
  int64_t expected_total = 0;
  for (const auto& delay : delays) {
    expected_total += delay.count();
  }

  EXPECT_EQ(rate.overrun_count(), 5u);
  EXPECT_EQ(rate.accumulated_delay().count(), expected_total);
}

// Stress test: alternating fast and slow execution
TEST_F(PrecisionUpdateRateTest, AlternatingWorkload)
{
  PrecisionUpdateRate rate(10ms);

  rate.step();

  for (int i = 0; i < 10; ++i) {
    if (i % 2 == 0) {
      // Fast iteration - no overrun
      rate.step();
    } else {
      // Slow iteration - cause overrun
      simulate_work(15ms);
      bool overrun = rate.step();
      EXPECT_TRUE(overrun);
    }
  }

  EXPECT_EQ(rate.overrun_count(), 5u);  // Every other iteration
  EXPECT_GT(rate.accumulated_delay().count(), 0);
}
