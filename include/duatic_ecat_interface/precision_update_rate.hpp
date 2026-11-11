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

#include <ctime>
#include <cstdint>
#include <chrono>

namespace duatic::ecat_interface
{

namespace precision_timing
{
constexpr int64_t billion = 1000000000;
constexpr int64_t sleep_early_stop_ns = 50000;

/**
 * @brief Compares two timespec structures to determine if the first is earlier than the second.
 * @param ts1 The first timespec to compare.
 * @param ts2 The second timespec to compare.
 * @return true if ts1 represents an earlier time than ts2, false otherwise.
 */
inline constexpr bool timespec_smaller_than(const timespec& ts1, const timespec& ts2)
{
  return (ts1.tv_sec < ts2.tv_sec || (ts1.tv_sec == ts2.tv_sec && ts1.tv_nsec < ts2.tv_nsec));
}

/**
 * @brief Calculates the difference between two timespec structures in nanoseconds.
 * @param a The timespec to subtract from.
 * @param b The timespec to subtract.
 * @return The difference (a - b) in nanoseconds.
 */
inline constexpr int64_t timespec_diff_ns(const timespec& a, const timespec& b)
{
  return (a.tv_sec - b.tv_sec) * billion + (a.tv_nsec - b.tv_nsec);
}

/**
 * @brief Adds a specified number of nanoseconds to a timespec structure.
 * @param t The base timespec.
 * @param ns The number of nanoseconds to add (can be negative).
 * @return A new timespec representing the adjusted time.
 */
inline constexpr struct timespec timespec_add_ns(timespec t, const int64_t ns)
{
  t.tv_sec += ns / billion;
  t.tv_nsec += ns % billion;

  if (t.tv_nsec >= billion) {
    t.tv_sec++;
    t.tv_nsec -= billion;
  } else if (t.tv_nsec < 0) {
    t.tv_sec--;
    t.tv_nsec += billion;
  }
  return t;
}

/**
 * @brief Gets the current monotonic time.
 * @return A timespec structure representing the current monotonic clock time.
 */
inline struct timespec now_monotonic()
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t;
}

/**
 * @brief Performs a high-precision sleep until the specified absolute time.
 *
 * Uses clock_nanosleep to sleep until shortly before the target time, then
 * busy-waits for the remaining duration to achieve nanosecond precision.
 *
 * @param ts The absolute target time (monotonic clock) to sleep until.
 */
inline void high_precision_sleep(timespec ts)
{
  timespec early = timespec_add_ns(ts, -sleep_early_stop_ns);
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &early, nullptr);

  // busy waiting for the remaining time
  timespec now;
  do {
    clock_gettime(CLOCK_MONOTONIC, &now);
  } while (timespec_smaller_than(now, ts));
}

}  // namespace precision_timing

/**
 * @brief Manages periodic execution at a fixed update rate with high precision timing.
 *
 * This class maintains a fixed-rate periodic timer that can detect and track timing
 * overruns (missed deadlines). It uses hybrid sleeping (clock_nanosleep + busy-wait)
 * to achieve high precision timing suitable for real-time control loops.
 */
class PrecisionUpdateRate
{
public:
  /**
   * @brief Constructs a PrecisionUpdateRate object with the specified time step.
   * @param time_step The desired period between successive steps.
   */
  explicit PrecisionUpdateRate(std::chrono::nanoseconds time_step) : time_step_ns_(time_step.count())
  {
    reset();
  }

  /**
   * @brief Resets the timer to start from the current time.
   *
   * Sets the next deadline to the current monotonic time. Useful for initialization
   * or resynchronization after a pause.
   */
  void reset()
  {
    sleep_end_ = precision_timing::now_monotonic();

    last_delay_ns_ = 0;
    accumulated_delay_ns_ = 0;
    overrun_count_ = 0;
  }

  /**
   * @brief Advances to the next time step and sleeps until the deadline.
   *
   * Advances the internal deadline by one time_step period and sleeps until that
   * deadline is reached. If the current time has already passed the deadline
   * (overrun), it tracks the delay statistics and adjusts the deadline to the
   * current time plus one time step.
   *
   * @return true if a timing overrun occurred (deadline was missed), false otherwise.
   */
  bool step(std::chrono::nanoseconds sync_offset = std::chrono::nanoseconds::zero())
  {
    using namespace precision_timing;  // NOLINT(build/namespaces)

    // advance fixed periodic deadline
    sleep_end_ = timespec_add_ns(sleep_end_, time_step_ns_ + sync_offset.count());
    last_delay_ns_ = 0;

    timespec now = now_monotonic();

    bool has_overrun = false;
    // we are late
    if (timespec_smaller_than(sleep_end_, now)) {
      has_overrun = true;
      overrun_count_ += 1;
      last_delay_ns_ = timespec_diff_ns(now, sleep_end_);
      accumulated_delay_ns_ += last_delay_ns_;
      sleep_end_ = now;

      sleep_end_ = timespec_add_ns(sleep_end_, time_step_ns_);
    }

    high_precision_sleep(sleep_end_);

    return has_overrun;
  }

  /**
   * @brief Gets the total accumulated delay from all timing overruns.
   * @return The total accumulated delay as a chrono::nanoseconds duration.
   */
  std::chrono::nanoseconds accumulated_delay() const
  {
    return std::chrono::nanoseconds(accumulated_delay_ns_);
  }

  /**
   * @brief Gets the delay from the most recent timing overrun.
   * @return The last delay as a chrono::nanoseconds duration (0 if no overrun occurred in last step).
   */
  std::chrono::nanoseconds last_delay() const
  {
    return std::chrono::nanoseconds(last_delay_ns_);
  }

  /**
   * @brief Gets the number of timing overruns that have occurred.
   * @return The count of missed deadlines since construction or last reset.
   */
  uint64_t overrun_count() const
  {
    return overrun_count_;
  }

private:
  int64_t time_step_ns_;
  int64_t accumulated_delay_ns_{ 0 };
  int64_t last_delay_ns_{ 0 };
  uint64_t overrun_count_{ 0 };
  timespec sleep_end_;
};

}  // namespace duatic::ecat_interface
