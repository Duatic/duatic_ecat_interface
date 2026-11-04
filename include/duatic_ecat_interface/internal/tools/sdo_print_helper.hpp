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

#include <array>        // NOLINT(build/include_order)
#include <charconv>     // NOLINT(build/include_order)
#include <span>         // NOLINT(build/include_order)
#include <string>       // NOLINT(build/include_order)
#include <type_traits>  // NOLINT(build/include_order)

#include "duatic_ecat_interface/ethercat_bus.hpp"

namespace duatic::ecat_interface::internal
{

// Shortest round-trip representation, same output as std::format("{}") (which GCC 11 / Humble lacks)
template <typename T>
std::string number_to_string(const T value)
{
  std::array<char, 64> buf{};
  const auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
  return ec == std::errc{} ? std::string(buf.data(), ptr) : "error";
}

// Only assumption about ValueDiagnosticsWrapper: contextual bool + .value
template <typename T>
std::string sdo_read_as(EthercatBus& bus, const DeviceId id, const SDOIndex index, const SDOSubIndex sub)
{
  const SDOReadValue<T> read = bus.sdo_read<T>(id, index, sub);
  if (!read.has_value()) {
    return "error";
  }
  if constexpr (std::is_same_v<T, bool>) {
    return read.value() ? "true" : "false";
  } else if constexpr (std::is_same_v<T, std::string>) {
    return read.value();
  } else {
    return number_to_string(read.value());
  }
}

// 24/40/48/56 bit: no native C++ type, read the bytes and assemble them.
inline std::string sdo_read_odd_int(EthercatBus& bus, const DeviceId id, const SDOIndex index, const SDOSubIndex sub,
                                    const std::size_t bytes, const bool is_signed)
{
  std::array<uint8_t, 8> buf{};
  if (!bus.read_sdo_untyped(std::span<uint8_t>(buf).first(bytes), id, index, sub)) {
    return "error";
  }

  uint64_t v = 0;
  for (std::size_t i = 0; i < bytes; ++i) {
    v |= static_cast<uint64_t>(buf[i]) << (8U * i);  // little endian on the wire
  }

  if (is_signed) {
    const uint64_t sign_bit = uint64_t{ 1 } << (bytes * 8U - 1U);
    return number_to_string(static_cast<int64_t>((v ^ sign_bit) - sign_bit));
  }
  return number_to_string(v);
}

inline std::string sdo_read_bits(EthercatBus& bus, const DeviceId id, const SDOIndex index, const SDOSubIndex sub,
                                 const unsigned bits)
{
  std::array<uint8_t, 1> buf{};
  if (!bus.read_sdo_untyped(buf, id, index, sub)) {
    return "error";
  }
  return number_to_string(buf[0] & ((1U << bits) - 1U));
}

inline std::string sdo_print_helper(EthercatBus& bus, const DeviceId device_id, const SDOIndex index,
                                    const SDOSubIndex sub_index, const DataType sdo_data_type)
{
  try {
    if (sdo_data_type >= DataType::BIT1 && sdo_data_type <= DataType::BIT8) {
      const unsigned bits = static_cast<uint16_t>(sdo_data_type) - static_cast<uint16_t>(DataType::BIT1) + 1U;
      return sdo_read_bits(bus, device_id, index, sub_index, bits);
    }

    switch (sdo_data_type) {
      case DataType::BOOLEAN:
        return sdo_read_as<bool>(bus, device_id, index, sub_index);
      case DataType::INTEGER8:
        return sdo_read_as<int8_t>(bus, device_id, index, sub_index);
      case DataType::INTEGER16:
        return sdo_read_as<int16_t>(bus, device_id, index, sub_index);
      case DataType::INTEGER32:
        return sdo_read_as<int32_t>(bus, device_id, index, sub_index);
      case DataType::INTEGER64:
        return sdo_read_as<int64_t>(bus, device_id, index, sub_index);
      case DataType::UNSIGNED8:
        return sdo_read_as<uint8_t>(bus, device_id, index, sub_index);
      case DataType::UNSIGNED16:
        return sdo_read_as<uint16_t>(bus, device_id, index, sub_index);
      case DataType::UNSIGNED32:
        return sdo_read_as<uint32_t>(bus, device_id, index, sub_index);
      case DataType::UNSIGNED64:
        return sdo_read_as<uint64_t>(bus, device_id, index, sub_index);
      case DataType::REAL32:
        return sdo_read_as<float>(bus, device_id, index, sub_index);
      case DataType::REAL64:
        return sdo_read_as<double>(bus, device_id, index, sub_index);
      case DataType::VISIBLE_STRING:
        return sdo_read_as<std::string>(bus, device_id, index, sub_index);

      case DataType::INTEGER24:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 3, true);
      case DataType::INTEGER40:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 5, true);
      case DataType::INTEGER48:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 6, true);
      case DataType::INTEGER56:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 7, true);
      case DataType::UNSIGNED24:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 3, false);
      case DataType::UNSIGNED40:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 5, false);
      case DataType::UNSIGNED48:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 6, false);
      case DataType::UNSIGNED56:
        return sdo_read_odd_int(bus, device_id, index, sub_index, 7, false);

      default:
        return "error";  // unsupported type
    }
  } catch (const std::runtime_error& ex) {
    return "error: " + std::string(ex.what());
  }
}
}  // namespace duatic::ecat_interface::internal
