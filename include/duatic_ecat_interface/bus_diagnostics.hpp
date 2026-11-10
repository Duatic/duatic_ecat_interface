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

#include <string>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>
#include <ostream>

#include "duatic_ecat_interface/types.hpp"

namespace duatic::ecat_interface
{

enum class DiagnosticsLevel
{
  // Diagnostics is disabled completely
  Off = 0,
  // Basic diagnostics is enabled
  Basic = 1,
  // Specific diagnostic events are also stored for later analysis (tracing)
  Tracing = 2
};

struct DiagnosticsOptions
{
  DiagnosticsOptions() = default;

  explicit DiagnosticsOptions(DiagnosticsLevel level)
  {
    pdo_diagnostics = level;
    mailbox_diagnostics = level;
    esc_diagnostics = level;
    esc_port_diagnostics = level;
  }

  // PDO related basic diagnostics such as WKC mismatches, lost frames, and total frames sent
  DiagnosticsLevel pdo_diagnostics{ DiagnosticsLevel::Basic };
  // Mailbox logging (Basic: emergencies, Tracing: every event) and the polling of the input mailboxes for emergencies
  // in update_service() - Off disables the polling, unsolicited messages then only arrive during accesses
  // Please note mailbox diagnostics related types are in the "types.hpp" header
  DiagnosticsLevel mailbox_diagnostics{ DiagnosticsLevel::Basic };
  // How often update_service() polls the input mailboxes, 0 disables the polling
  std::chrono::milliseconds mailbox_poll_interval{ 100 };
  // Diagnostics related to esc (this is mostly tracking state changes of an esc)
  DiagnosticsLevel esc_diagnostics{ DiagnosticsLevel::Off };
  // Diagnostics related to every port of an esc (very expensive diagnostics)
  DiagnosticsLevel esc_port_diagnostics{ DiagnosticsLevel::Off };
};

/**
 * @brief is_diagnostics_enabled - check if the diagnostics level is higher than 'Off'
 */
constexpr bool is_diagnostics_enabled(const DiagnosticsLevel level)
{
  return level != DiagnosticsLevel::Off;
}

// AL Status Code as defined by ETG.1000/ETG.1020 — the vendor-independent
// reason code a slave reports alongside its state (e.g. why it refused a
// state transition). 0x0000 conventionally means "no error".
using AlStatus = uint16_t;

// Decodes an AlStatus into a human-readable description via a static
// ETG.1020 lookup table. Unknown/vendor-specific codes should return a
// generic "unknown (0x....)" string rather than throwing.
std::string to_string(const AlStatus& status);

// Per-port health of a slave's ESC (EtherCAT Slave Controller). Sourced
// directly from ESC DL-layer registers (DL Status, RX Error Counters) —
// not from the slave's application layer.
struct ESCPortHealth
{
  // True if this physical port currently has an active link (cable
  // connected, PHY synced). Ports not physically wired on a given slave
  // will always read false.
  bool link_up{ false };

  // Cumulative count of frames received on this port that failed
  // EtherCAT frame validation (e.g. bad length/format), as reported by
  // the ESC's invalid-frame counter.
  uint32_t invalid_frames{ 0 };

  // Cumulative count of frames received on this port with a physical
  // layer/CRC error, as reported by the ESC's RX error counter. Distinct
  // from invalid_frames: this indicates link-quality issues (cabling,
  // EMI), whereas invalid_frames indicates protocol-level malformation.
  uint32_t rx_errors{ 0 };

  // Number of times the link on this port has dropped and re-established
  // since the master started monitoring this slave. Useful for catching
  // intermittent connections that a single link_up snapshot would miss.
  uint32_t lost_links{ 0 };
};

// One-time topology/DC-propagation-delay facts SOEM computes during bus bring-up. `parent` and
// `active_ports` are valid as soon as initialize() has run; `parent_port`/`entry_port`/
// `propagation_delay_ns`/`dc_next`/`dc_previous`/`dc_port_receive_time_ns` are only valid once
// startup() has completed its DC configuration pass - reading them any earlier yields zero.
// Unlike the rest of ESCStatus, none of these are gated behind DiagnosticsOptions (they cost
// nothing to capture) and none are refreshed again after bring-up.
//
// Note: `active_ports` here is a permanent bring-up snapshot, distinct from the live,
// round-robin-refreshed `ESCPortHealth::link_up` on the same slave's `ports` array - the two can
// legitimately disagree after bring-up (e.g. a port that comes up later, or a cable pulled after
// initialize() ran).
struct DeviceTopology
{
  DeviceId parent = 0;                               // parent slave id in the physical chain, 0 = master
  uint8_t parent_port = 0;                           // port on the parent this slave is connected to
  uint8_t entry_port = 0;                            // port on this slave the parent is connected to
  std::array<bool, 4> active_ports{};                // active_ports[N] true if port N has an active link
  int32_t propagation_delay_ns = 0;                  // one-time DC propagation delay measurement, nanoseconds
  DeviceId dc_next = 0;                              // next slave in the DC sync chain, 0 = none
  DeviceId dc_previous = 0;                          // previous slave in the DC sync chain, 0 = none/master
  std::array<int32_t, 4> dc_port_receive_time_ns{};  // per-port DC receive timestamps (DCrtA-D)
};

// Diagnostic status of a single slave's ESC. All fields except `online`
// are sourced from ESC registers (AL Status, AL Status Code, DL layer);
// `online` is inferred by the master, not read from the slave.
struct ESCStatus
{
  // The slave's position in the bus ring (auto-increment address order).
  // Used to correlate this entry with the slave's configuration.
  uint16_t position = 0;

  // Current EtherCAT state machine (ESM) state of this slave, as reported
  // in its AL Status register.
  EthercatDeviceState state = EthercatDeviceState::None;

  // AL Status Code accompanying `state` — explains *why* the slave is in
  // its current state, particularly relevant after a fault or a rejected
  // state transition request. Only valid while `error_indication` is set,
  // 0 otherwise (ETG.1000.6: the master shall ignore it then).
  AlStatus al_status{};

  // Error Flag of the AL Status: the slave refused a requested state
  // transition or left a state on its own due to an error. The master
  // acknowledges it with the next state change requested for this slave.
  // Like `state` and `al_status` it is only refreshed after activation
  // while the ESC diagnostics are enabled.
  bool error_indication = false;

  // True if the slave's ESC emulates the AL Status (simple slave without
  // application controller, read once from the PDI Control register at
  // initialization). Errors of such slaves are not acknowledged.
  bool device_emulation = false;

  // True if the master is currently receiving valid responses from this
  // slave (i.e. it responded and contributed to the working counter on
  // the last cycle). False if the slave is known/configured but not
  // currently answering (unplugged, powered off, dropped from the ring).
  bool online = false;

  // Per-port ESC health. Slaves expose up to 4 ports as defined by the
  // ESC register map, regardless of how many are physically wired;
  // unused ports simply report link_up == false.
  std::array<ESCPortHealth, 4> ports{};

  // As the ESC diagnostics is costly to read we maintain separate timestamps
  // for the port diagnostics
  HighPrecisionTimeStamp ports_update_timestamp;

  // This slave's position in the physical chain - see DeviceTopology's own doc comment for the
  // two-phase validity caveat.
  DeviceTopology topology{};
};

// Bus-wide diagnostics, sourced from the master's own telemetry rather
// than from any single slave's ESC.
struct BusStatus
{
  // Physical link state of the master's own network interface (nic::is_up(): administratively
  // up per SIOCGIFFLAGS, not a carrier/cable-present check). Distinct from any slave's
  // ESCPortHealth::link_up. Captured once at initialize(), then refreshed every service-thread
  // tick (~every service_thread_update_rate, 2 ms by default) for as long as a SingleBusExecutor
  // is spinning - a cable pulled mid-run will show up here within about one tick. If no executor
  // is ever spun (e.g. a bus used only for scan()/SDO access), this stays at its
  // initialize()-time value.
  bool link_up = true;

  // frames_sent/frames_lost/wkc_mismatches below are only incremented while pdo_diagnostics is
  // enabled (default Basic), and accumulate for the entire lifetime of the owning EthercatBus
  // instance - there is no reset anywhere in initialize()/startup()/activate()/shutdown(), and a
  // slave dropping/reconnecting does not reset them either. They only start over when a new
  // EthercatBus object is constructed.

  // Cumulative number of EtherCAT frames sent by the master since this EthercatBus was
  // constructed.
  uint64_t frames_sent = 0;

  // Cumulative number of frames for which no valid response was received
  // before timeout, as determined by the master's cyclic exchange.
  uint64_t frames_lost = 0;

  // Cumulative number of cycles where the returned working counter did
  // not match the expected value for the process data exchanged.
  uint64_t wkc_mismatches = 0;
};

// Cumulative mailbox event counters (CoE/FoE/SoE aborts, timeouts, emergencies), accumulated for
// the entire lifetime of the owning EthercatBus instance (same never-reset semantics as
// BusStatus's counters above). Every event is counted, independent of the mailbox_diagnostics level.
struct MailboxStatus
{
  uint64_t mailbox_aborts = 0;
  uint64_t mailbox_timeouts = 0;
  uint64_t mailbox_emergencies = 0;
  // Events dropped from the full EthercatBus::pop_mailbox_events() queue
  uint64_t mailbox_events_dropped = 0;
};

struct ExecutorTimingDiagnostics
{
  // Last point in time the specific executor thread update has happened
  HighPrecisionTimeStamp last_update_tp;
  // Interval at which the last update happened
  std::chrono::microseconds last_update_rate;
  // Amount of time the last update step took
  std::chrono::microseconds last_update_duration;
  // Average update rate of the executor in Hz
  double average_update_rate{ 0.0 };
  // Average amount of time the update takes in us
  double average_update_duration{ 0.0 };
  // How much delay we accumulated in total due to missed deadlines
  // This is optional and only relevant for the RT thread
  std::optional<std::chrono::nanoseconds> accumulated_delay;
  // Amount of missed timing deadlines
  // This is optional and only relevant for the RT thread
  std::optional<uint64_t> missed_rate_steps;
  // indicate if the loop of this specific executor thread is running (false again once it ended, e.g. after an
  // error) - unlike ExecutionStatus::is_spinning, which only tells that the executor was started
  bool is_running{ false };
};

// Executor diagnostics which is added by the executor to a diagnostics snapshot
struct ExecutionStatus
{
  // Indicate whether the executor has been started (spin() called, stop() not yet) - see
  // ExecutorTimingDiagnostics::is_running for whether its threads are actually alive
  bool is_spinning = false;
  // Statistics for the realtime thread
  ExecutorTimingDiagnostics rt_thread_stats;
  // Statistics for the service thread
  ExecutorTimingDiagnostics service_thread_stats;
};

// A consistent, point-in-time view of bus and slave diagnostics.
struct DiagnosticsSnapshot
{
  BusStatus bus;
  std::vector<ESCStatus> slaves;

  // Live copy of the bus's cumulative mailbox event counters, taken at the moment diagnostics()
  // was called - unlike `bus`/`slaves`, this is not itself part of the RT-cycle-updated snapshot
  // cache, since mailbox events are never reported from the RT thread.
  MailboxStatus mailbox;

  // Additional information added by the executor if the DiagnosticsSnapshot was obtained from the executor
  // Otherwise this field will be empty
  std::optional<ExecutionStatus> executor;
  // Time this snapshot was captured, using a monotonic clock — suitable
  // for measuring elapsed time between snapshots, not wall-clock display.
  // @note this does not refer to all subfields!
  HighPrecisionTimeStamp timestamp;
};

std::string to_string(const DiagnosticsSnapshot& snap);
std::ostream& operator<<(std::ostream& os, const DiagnosticsSnapshot& snap);

}  // namespace duatic::ecat_interface
