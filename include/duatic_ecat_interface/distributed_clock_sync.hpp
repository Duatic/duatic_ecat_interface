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
#pragma once

#include <chrono>
#include <cstdint>

namespace duatic::ecat_interface
{

class DCSyncController
{
public:
  DCSyncController(std::chrono::nanoseconds cycle_time,
                   std::chrono::nanoseconds sync_offset = std::chrono::nanoseconds::zero(), double p_gain = 0.01,
                   double i_gain = 0.00002)
    : cycle_time_(cycle_time), sync_offset_(sync_offset), p_gain_(p_gain), i_gain_(i_gain)
  {
  }

  /**
   * @brief Feed the latest DC reference time, get the correction to
   *        apply to the master's next cycle deadline.
   * @param dc_time  Reference slave's DC system time (ec_DCtime),
   *                 sampled right after ec_receive_processdata().
   */
  std::chrono::nanoseconds update(std::chrono::nanoseconds dc_time)
  {
    auto cycle_count = cycle_time_.count();
    auto delta = (dc_time.count() - sync_offset_.count()) % cycle_count;
    if (delta > cycle_count / 2) {
      delta -= cycle_count;
    }

    last_time_error_ = std::chrono::nanoseconds{ -delta };
    integral_ += static_cast<double>(last_time_error_.count());

    auto offset_count =
        static_cast<int64_t>(static_cast<double>(last_time_error_.count()) * p_gain_ + integral_ * i_gain_);

    return std::chrono::nanoseconds{ offset_count };
  }

  void reset()
  {
    integral_ = 0.0;
    last_time_error_ = std::chrono::nanoseconds::zero();
  }

  std::chrono::nanoseconds last_time_error() const
  {
    return last_time_error_;
  }

private:
  std::chrono::nanoseconds cycle_time_;
  std::chrono::nanoseconds sync_offset_;
  double p_gain_;
  double i_gain_;
  double integral_ = 0.0;  // double, not float — avoids long-run precision loss
  std::chrono::nanoseconds last_time_error_{ 0 };
};
}  // namespace duatic::ecat_interface
