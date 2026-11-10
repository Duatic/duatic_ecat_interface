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

#include <span>
#include <cstdint>
#include <functional>
#include <ostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <sstream>
#include <optional>
#include <array>

#include "duatic_ecat_interface/value_diagnostics_wrapper.hpp"

namespace duatic::ecat_interface
{
// Timing types used within the ethercat sdk
using StandardClock = std::chrono::system_clock;
using StandardTimeStamp = std::chrono::system_clock::time_point;

using HighPrecisionClock = std::chrono::steady_clock;
using HighPrecisionTimeStamp = std::chrono::steady_clock::time_point;

// General ethercat types
using DeviceId = uint16_t;
constexpr std::size_t maximum_visible_string_size = 255;

// Always available information in an ethercat device
struct DeviceInfo
{
  DeviceId id{};
  std::string name{};
  uint32_t vendor_id{};
  uint32_t product_id{};
  uint32_t revision{};
  bool has_dc{};
};

inline std::string to_string(const DeviceInfo& device)
{
  std::ostringstream os;

  os << "DeviceInfo{\n"
     << "  id: " << static_cast<int>(device.id) << ",\n"
     << "  name: \"" << device.name << "\",\n"
     << "  vendor_id: 0x" << std::hex << std::uppercase << device.vendor_id << ",\n"
     << "  product_id: 0x" << device.product_id << ",\n"
     << "  revision: 0x" << device.revision << ",\n"
     << "  has_dc: " << std::boolalpha << device.has_dc << "\n"
     << "}";

  return os.str();
}

inline std::ostream& operator<<(std::ostream& os, const DeviceInfo& device)
{
  return os << to_string(device);
}

// A device the network is configured to have at a given position on the bus
struct ExpectedDevice
{
  DeviceId id{};
  uint32_t vendor_id{};
  uint32_t product_id{};
  // Any revision is accepted when not set
  std::optional<uint32_t> revision{};
};

// Representation of ethercat device states
enum class EthercatDeviceState
{
  None,
  Init,
  PreOp,
  Boot,
  SafeOp,
  Operational,
};

inline std::string to_string(const EthercatDeviceState state)
{
  switch (state) {
    case EthercatDeviceState::None:
      return "None";
    case EthercatDeviceState::Init:
      return "Init";
    case EthercatDeviceState::PreOp:
      return "PreOp";
    case EthercatDeviceState::Boot:
      return "Boot";
    case EthercatDeviceState::SafeOp:
      return "SafeOp";
    case EthercatDeviceState::Operational:
      return "Operational";
  }
  return "Invalid (" + std::to_string(static_cast<int>(state)) + ")";
}

inline std::ostream& operator<<(std::ostream& os, const EthercatDeviceState state)
{
  return os << to_string(state);
}

// Ethercat Register access types
using RegisterAddress = uint16_t;

// Types for Ethercat Register interactions
struct RegisterWriteResult
{
  bool success{ false };
  int working_counter{ 0 };

  explicit operator bool() const
  {
    return success;
  }
};

struct RegisterReadResult
{
  bool success{ false };
  int working_counter{ 0 };
  std::size_t actual_size_read{ 0 };
  std::span<const uint8_t> data{};

  explicit operator bool() const
  {
    return success;
  }
};

template <typename T>
using RegisterReadValue = ValueDiagnosticsWrapper<T, RegisterReadResult>;

// EEPROM (SII) access types, the EEPROM is addressed in 16 bit words
using EepromWordAddress = uint16_t;

// Unlike the other results there is no working counter: an eeprom read is a sequence of several register accesses
struct EepromReadResult
{
  bool success{ false };
  std::size_t actual_size_read{ 0 };
  std::span<const uint8_t> data{};

  explicit operator bool() const
  {
    return success;
  }
};

// CoE types
using SDOIndex = uint16_t;
using SDOSubIndex = uint8_t;

/* @brief EthercatValueType - the set of trivial, fixed-width value types that the backend can transfer
 * byte-for-byte over SDO or ESC register access
 */
template <typename T>
concept EthercatValueType = std::same_as<T, bool> || std::same_as<T, int8_t> || std::same_as<T, uint8_t> ||
    std::same_as<T, int16_t> || std::same_as<T, uint16_t> || std::same_as<T, int32_t> || std::same_as<T, uint32_t> ||
    std::same_as<T, int64_t> || std::same_as<T, uint64_t> || std::same_as<T, float> || std::same_as<T, double>;

template <typename T>
concept SdoValueType = EthercatValueType<T> || std::same_as<T, std::string>;

// Types for mailbox diagnostics
// These are on purpose located in this file as they are used in the access return values
enum class MailboxProtocol : uint8_t
{
  Unknown,
  CoE,
  FoE,
  SoE,
  EoE,
  VoE
};

enum class MailboxEventKind : uint8_t
{
  Unknown,
  Abort,              ///< Device rejected the service (CoE abort, SoE error, FoE error)
  Emergency,          ///< Unsolicited device notification - NOT a failure of your request
  ProtocolViolation,  ///< Malformed or unexpected response; master-side parse failure
  MailboxLayerError,  ///< Mailbox-layer error response (syntax, unsupported protocol, ...)
  Timeout,
  BufferTooSmall,
  NotFound  ///< FoE file not found; object/subindex absent where distinguishable
};

enum class MailboxEventSeverity : uint8_t
{
  Info,
  Warning,
  Error
};

/// CoE emergency payload. This is the 8-byte ETG/CiA 301 emergency frame
struct EmergencyInfo
{
  uint16_t code{};                ///< 0x0000 == error reset / no error
  uint8_t error_register{};       ///< mirror of object 0x1001
  std::array<uint8_t, 5> data{};  ///< manufacturer-specific

  bool is_reset() const
  {
    return code == 0x0000;
  }
};

/// A single event reported by a device (or by the master) over the mailbox channel.
struct MailboxEvent
{
  HighPrecisionTimeStamp timestamp{};
  DeviceId device_id{};

  MailboxProtocol protocol{ MailboxProtocol::Unknown };
  MailboxEventKind kind{ MailboxEventKind::Unknown };
  MailboxEventSeverity severity{ MailboxEventSeverity::Error };

  /// Protocol-scoped numeric code, interpreted together with `protocol`:
  /// CoE -> 32-bit abort code (0x06020000, ...), SoE -> 16-bit error code,
  /// FoE -> 32-bit error code, mailbox layer -> 16-bit detail. 0 if not applicable.
  uint32_t code{};

  /// Present only when the backend can attribute the event to a specific access.
  std::optional<SDOIndex> index{};
  std::optional<SDOSubIndex> sub_index{};

  /// Set if kind == Emergency.
  std::optional<EmergencyInfo> emergency{};

  /// Rendered by the backend, since only it can decode its own codes.
  std::string description{};

  /// A real device fault, as opposed to a fault-cleared notification.
  bool is_device_fault() const
  {
    return kind == MailboxEventKind::Emergency && emergency && !emergency->is_reset();
  }
};

// Types for SDO (ServiceDataObject) interactions
// The read data is in the buffer passed by the caller, so unlike the register / eeprom results there is no data span
struct SDOReadResult
{
  bool success{ false };
  int actual_size_read{ 0 };
  int working_counter{ 0 };

  std::optional<MailboxEvent> mailbox_diagnostics{};

  explicit operator bool() const
  {
    return success;
  }
};

template <typename T>
using SDOReadValue = ValueDiagnosticsWrapper<T, SDOReadResult>;

struct SDOWriteResult
{
  bool success{ false };
  int working_counter{ 0 };

  std::optional<MailboxEvent> mailbox_diagnostics{};

  explicit operator bool() const
  {
    return success;
  }
};

using SDOReadCallback = std::function<void(std::span<const uint8_t>, const DeviceId, const SDOIndex, const SDOSubIndex,
                                           const SDOReadResult&)>;
using SDOWriteCallback = std::function<void(const DeviceId, const SDOIndex, const SDOSubIndex, const SDOWriteResult&)>;

// Representation of various types of SDO object codes
enum class SDOObjectCode : uint8_t
{
  Var = 0x07,
  Array = 0x08,
  Record = 0x09
};

inline std::string to_string(const SDOObjectCode code)
{
  switch (code) {
    case SDOObjectCode::Var:
      return "Var";
    case SDOObjectCode::Array:
      return "Array";
    case SDOObjectCode::Record:
      return "Record";
  }
  return "Invalid (" + std::to_string(static_cast<int>(code)) + ")";
}

inline std::ostream& operator<<(std::ostream& os, const SDOObjectCode code)
{
  return os << to_string(code);
}

// Representation of supported data types - the enumerators keep the ETG.1000.6 / ETG.1020 data type names
enum class DataType : uint16_t
{
  UNKNOWN = 0x0000,

  BOOLEAN = 0x0001,
  INTEGER8 = 0x0002,
  INTEGER16 = 0x0003,
  INTEGER32 = 0x0004,
  UNSIGNED8 = 0x0005,
  UNSIGNED16 = 0x0006,
  UNSIGNED32 = 0x0007,
  REAL32 = 0x0008,
  VISIBLE_STRING = 0x0009,
  OCTET_STRING = 0x000A,
  UNICODE_STRING = 0x000B,
  TIME_OF_DAY = 0x000C,
  TIME_DIFFERENCE = 0x000D,

  DOMAIN = 0x000F,

  INTEGER24 = 0x0010,
  REAL64 = 0x0011,
  INTEGER40 = 0x0012,
  INTEGER48 = 0x0013,
  INTEGER56 = 0x0014,
  INTEGER64 = 0x0015,

  UNSIGNED24 = 0x0016,

  UNSIGNED40 = 0x0018,
  UNSIGNED48 = 0x0019,
  UNSIGNED56 = 0x001A,
  UNSIGNED64 = 0x001B,

  BIT1 = 0x0030,
  BIT2 = 0x0031,
  BIT3 = 0x0032,
  BIT4 = 0x0033,
  BIT5 = 0x0034,
  BIT6 = 0x0035,
  BIT7 = 0x0036,
  BIT8 = 0x0037,
};

inline std::string to_string(const DataType type)
{
  switch (type) {
    case DataType::UNKNOWN:
      return "UNKNOWN";
    case DataType::BOOLEAN:
      return "BOOLEAN";
    case DataType::INTEGER8:
      return "INTEGER8";
    case DataType::INTEGER16:
      return "INTEGER16";
    case DataType::INTEGER32:
      return "INTEGER32";
    case DataType::UNSIGNED8:
      return "UNSIGNED8";
    case DataType::UNSIGNED16:
      return "UNSIGNED16";
    case DataType::UNSIGNED32:
      return "UNSIGNED32";
    case DataType::REAL32:
      return "REAL32";
    case DataType::VISIBLE_STRING:
      return "VISIBLE_STRING";
    case DataType::OCTET_STRING:
      return "OCTET_STRING";
    case DataType::UNICODE_STRING:
      return "UNICODE_STRING";
    case DataType::TIME_OF_DAY:
      return "TIME_OF_DAY";
    case DataType::TIME_DIFFERENCE:
      return "TIME_DIFFERENCE";
    case DataType::DOMAIN:
      return "DOMAIN";
    case DataType::INTEGER24:
      return "INTEGER24";
    case DataType::REAL64:
      return "REAL64";
    case DataType::INTEGER40:
      return "INTEGER40";
    case DataType::INTEGER48:
      return "INTEGER48";
    case DataType::INTEGER56:
      return "INTEGER56";
    case DataType::INTEGER64:
      return "INTEGER64";
    case DataType::UNSIGNED24:
      return "UNSIGNED24";
    case DataType::UNSIGNED40:
      return "UNSIGNED40";
    case DataType::UNSIGNED48:
      return "UNSIGNED48";
    case DataType::UNSIGNED56:
      return "UNSIGNED56";
    case DataType::UNSIGNED64:
      return "UNSIGNED64";
    case DataType::BIT1:
      return "BIT1";
    case DataType::BIT2:
      return "BIT2";
    case DataType::BIT3:
      return "BIT3";
    case DataType::BIT4:
      return "BIT4";
    case DataType::BIT5:
      return "BIT5";
    case DataType::BIT6:
      return "BIT6";
    case DataType::BIT7:
      return "BIT7";
    case DataType::BIT8:
      return "BIT8";
  }

  return "Invalid (" + std::to_string(static_cast<int>(type)) + ")";
}

inline std::ostream& operator<<(std::ostream& os, const DataType type)
{
  return os << to_string(type);
}

// Types for FileOverEthercat interactions
struct FoEWriteResult
{
  bool success{ false };
  int working_counter{ 0 };

  std::optional<MailboxEvent> mailbox_diagnostics{};

  explicit operator bool() const
  {
    return success;
  }
};

struct FoEReadResult
{
  bool success{ false };
  int working_counter{ 0 };
  std::size_t actual_read_size{ 0 };

  std::optional<MailboxEvent> mailbox_diagnostics{};

  explicit operator bool() const
  {
    return success;
  }
};

using FoEReadValue = ValueDiagnosticsWrapper<std::span<const uint8_t>, FoEReadResult>;

}  // namespace duatic::ecat_interface
