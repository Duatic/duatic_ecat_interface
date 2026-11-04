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

#include <soem_vendor/ethercat.h>

#include <cstdint>

namespace duatic::ecat_interface::internal::soem
{

enum class BusState
{
  PreInit,
  Initialized,
  Configured,
  Activated,
  Operational,
  ShuttingDown,
  Shutdown
};

inline std::ostream& operator<<(std::ostream& os, BusState state)
{
  switch (state) {
    case BusState::PreInit:
      return os << "PreInit";
    case BusState::Initialized:
      return os << "Initialized";
    case BusState::Configured:
      return os << "Configured";
    case BusState::Activated:
      return os << "Activated";
    case BusState::Operational:
      return os << "Operational";
    case BusState::ShuttingDown:
      return os << "ShuttingDown";
    case BusState::Shutdown:
      return os << "Shutdown";
  }

  return os << "Unknown";
}

static constexpr ec_state map_to_soem_device_state(const EthercatDeviceState state)
{
  switch (state) {
    case EthercatDeviceState::None:
      return ec_state::EC_STATE_NONE;
    case EthercatDeviceState::Init:
      return ec_state::EC_STATE_INIT;
    case EthercatDeviceState::PreOp:
      return ec_state::EC_STATE_PRE_OP;
    case EthercatDeviceState::Boot:
      return ec_state::EC_STATE_BOOT;
    case EthercatDeviceState::SafeOp:
      return ec_state::EC_STATE_SAFE_OP;
    case EthercatDeviceState::Operational:
      return ec_state::EC_STATE_OPERATIONAL;

    default:
      return ec_state::EC_STATE_NONE;
  }
}

static constexpr EthercatDeviceState map_from_soem_device_state(const ec_state state)
{
  switch (state) {
    case ec_state::EC_STATE_NONE:
      return EthercatDeviceState::None;
    case ec_state::EC_STATE_INIT:
      return EthercatDeviceState::Init;
    case ec_state::EC_STATE_PRE_OP:
      return EthercatDeviceState::PreOp;
    case ec_state::EC_STATE_BOOT:
      return EthercatDeviceState::Boot;
    case ec_state::EC_STATE_SAFE_OP:
      return EthercatDeviceState::SafeOp;
    case ec_state::EC_STATE_OPERATIONAL:
      return EthercatDeviceState::Operational;
    case ec_state::EC_STATE_ACK:
      // This state is just an additional status bit
      return EthercatDeviceState::None;
    default:
      return EthercatDeviceState::None;
  }
}
}  // namespace duatic::ecat_interface::internal::soem
