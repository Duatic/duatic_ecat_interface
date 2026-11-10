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

#include "duatic_ecat_interface/internal/soem/bus_state.hpp"

using namespace duatic::ecat_interface;                  // NOLINT(build/namespaces)
using namespace duatic::ecat_interface::internal::soem;  // NOLINT(build/namespaces)
using std::chrono::milliseconds;

namespace
{
EthercatBus::Parameters parameters()
{
  EthercatBus::Parameters params;
  params.timeout_transition_operational = milliseconds{ 1 };
  params.timeout_transition_preop = milliseconds{ 2 };
  params.timeout_transition_back_to_init = milliseconds{ 3 };
  params.timeout_transition_back_to_safeop = milliseconds{ 4 };
  return params;
}
}  // namespace

// The mapping of ETG.2000 Device/Info/StateMachine/Timeout
TEST(TransitionTimeout, FollowsTheEsiTimeoutElements)
{
  const auto params = parameters();
  // PreopTimeout: Init -> PreOp/Boot
  EXPECT_EQ(transition_timeout(params, EC_STATE_INIT, EC_STATE_PRE_OP), milliseconds{ 2 });
  EXPECT_EQ(transition_timeout(params, EC_STATE_INIT, EC_STATE_BOOT), milliseconds{ 2 });
  // SafeopOpTimeout: PreOp -> SafeOp and SafeOp -> Op
  EXPECT_EQ(transition_timeout(params, EC_STATE_PRE_OP, EC_STATE_SAFE_OP), milliseconds{ 1 });
  EXPECT_EQ(transition_timeout(params, EC_STATE_SAFE_OP, EC_STATE_OPERATIONAL), milliseconds{ 1 });
  // BackToInitTimeout: any state -> Init and Op/SafeOp -> PreOp
  EXPECT_EQ(transition_timeout(params, EC_STATE_OPERATIONAL, EC_STATE_INIT), milliseconds{ 3 });
  EXPECT_EQ(transition_timeout(params, EC_STATE_BOOT, EC_STATE_INIT), milliseconds{ 3 });
  EXPECT_EQ(transition_timeout(params, EC_STATE_OPERATIONAL, EC_STATE_PRE_OP), milliseconds{ 3 });
  EXPECT_EQ(transition_timeout(params, EC_STATE_SAFE_OP, EC_STATE_PRE_OP), milliseconds{ 3 });
  // BackToSafeopTimeout: Op -> SafeOp
  EXPECT_EQ(transition_timeout(params, EC_STATE_OPERATIONAL, EC_STATE_SAFE_OP), milliseconds{ 4 });
}

TEST(TransitionTimeout, IgnoresTheErrorIndication)
{
  const auto from = static_cast<ec_state>(EC_STATE_SAFE_OP | EC_STATE_ERROR);
  EXPECT_EQ(transition_timeout(parameters(), from, EC_STATE_PRE_OP), milliseconds{ 3 });
}

TEST(TransitionTimeout, DisabledOperationalTimeoutKeepsPreopToSafeopBounded)
{
  auto params = parameters();
  params.timeout_transition_operational = milliseconds{ 0 };
  EXPECT_EQ(transition_timeout(params, EC_STATE_PRE_OP, EC_STATE_SAFE_OP), milliseconds{ 10000 });
}

TEST(Remaining, IsTheTimeLeftUntilTheDeadlineRoundedUp)
{
  const auto now = HighPrecisionClock::now();
  EXPECT_EQ(remaining(now + milliseconds{ 5 }, now), milliseconds{ 5 });
  EXPECT_EQ(remaining(now + std::chrono::microseconds{ 4500 }, now), milliseconds{ 5 });
}

TEST(Remaining, IsZeroOnceTheDeadlinePassed)
{
  const auto now = HighPrecisionClock::now();
  EXPECT_EQ(remaining(now, now), milliseconds{ 0 });
  EXPECT_EQ(remaining(now - milliseconds{ 5 }, now), milliseconds{ 0 });
}

// ETG.2000 Device/Info/StateMachine/Timeout defaults, used as long as no ESI values are configured
TEST(TransitionTimeout, DefaultsAreTheEtg2000Defaults)
{
  const EthercatBus::Parameters params;
  EXPECT_EQ(params.timeout_transition_preop, milliseconds{ 3000 });
  EXPECT_EQ(params.timeout_transition_operational, milliseconds{ 10000 });
  EXPECT_EQ(params.timeout_transition_back_to_init, milliseconds{ 5000 });
  EXPECT_EQ(params.timeout_transition_back_to_safeop, milliseconds{ 200 });
}

TEST(AlControlRequest, AcknowledgesAPendingError)
{
  const auto al_status = static_cast<uint16_t>(EC_STATE_SAFE_OP | EC_STATE_ERROR);
  EXPECT_EQ(al_control_request(EC_STATE_OPERATIONAL, al_status), EC_STATE_OPERATIONAL | EC_STATE_ACK);
  EXPECT_EQ(al_control_request(EC_STATE_PRE_OP, al_status), EC_STATE_PRE_OP | EC_STATE_ACK);
}

TEST(AlControlRequest, IsThePlainStateWithoutError)
{
  for (const auto state : { EC_STATE_INIT, EC_STATE_PRE_OP, EC_STATE_BOOT, EC_STATE_SAFE_OP, EC_STATE_OPERATIONAL }) {
    EXPECT_EQ(al_control_request(state, EC_STATE_SAFE_OP), state);
  }
}

TEST(AlStatus, ErrorIndicationIsTheErrorFlag)
{
  EXPECT_TRUE(has_error_indication(EC_STATE_SAFE_OP | EC_STATE_ERROR));
  EXPECT_FALSE(has_error_indication(EC_STATE_SAFE_OP));
}

TEST(AlStatus, StatusCodeIsIgnoredWithoutErrorFlag)
{
  EXPECT_EQ(valid_al_status_code(EC_STATE_SAFE_OP | EC_STATE_ERROR, 0x001A), 0x001A);
  EXPECT_EQ(valid_al_status_code(EC_STATE_SAFE_OP, 0x001A), 0);
}
