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
#include "duatic_ecat_interface/bus_diagnostics.hpp"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace duatic::ecat_interface
{
namespace
{
void print_timing(std::ostream& os, const char* label, const ExecutorTimingDiagnostics& t)
{
  const auto flags = os.flags();
  const auto prec = os.precision();

  os << "  " << label << ":\n";
  os << "    last update:       " << t.last_update_tp.time_since_epoch().count() << " ns (monotonic)\n";
  os << "    last update rate:  " << t.last_update_rate.count() << " us\n";
  os << "    last duration:     " << t.last_update_duration.count() << " us\n";
  os << std::fixed << std::setprecision(6);
  os << "    avg update rate:   " << t.average_update_rate << " Hz\n";
  os << "    avg duration:      " << t.average_update_duration << " us\n";
  os.flags(flags);
  os.precision(prec);
  os << "    running:           " << (t.is_running ? "yes" : "no") << "\n";

  // RT-thread-only fields
  if (t.missed_rate_steps) {
    os << "    missed rate steps: " << *t.missed_rate_steps << "\n";
  }
  if (t.accumulated_delay) {
    os << "    accumulated delay: " << t.accumulated_delay->count() << " ns\n";
  }
}
}  // namespace

std::string to_string(const DiagnosticsSnapshot& snap)
{
  std::ostringstream os;

  os << "=== EtherCAT Diagnostics ===\n";
  os << "snapshot timestamp: " << snap.timestamp.time_since_epoch().count() << " ns (monotonic)\n";

  // Bus-wide
  os << "\n-- Bus --\n";
  os << "  master link:    " << (snap.bus.link_up ? "up" : "down") << "\n";
  os << "  frames sent:    " << snap.bus.frames_sent << "\n";
  os << "  frames lost:    " << snap.bus.frames_lost << "\n";
  os << "  wkc mismatches: " << snap.bus.wkc_mismatches << "\n";

  // Mailbox (cumulative, for the lifetime of the bus)
  os << "\n-- Mailbox --\n";
  os << "  aborts:      " << snap.mailbox.mailbox_aborts << "\n";
  os << "  timeouts:    " << snap.mailbox.mailbox_timeouts << "\n";
  os << "  emergencies: " << snap.mailbox.mailbox_emergencies << "\n";

  // Per-slave
  os << "\n-- Slaves (" << snap.slaves.size() << ") --\n";
  for (const auto& s : snap.slaves) {
    os << "  [" << std::setw(2) << s.position << "] " << std::setw(11) << std::left << to_string(s.state) << std::right
       << " | " << (s.online ? "online " : "OFFLINE") << " | AL 0x" << std::setfill('0') << std::setw(4) << std::hex
       << s.al_status << std::dec << std::setfill(' ') << " (" << to_string(s.al_status) << ")\n";

    os << "        ports (updated " << s.ports_update_timestamp.time_since_epoch().count() << " ns):\n";
    for (std::size_t p = 0; p < s.ports.size(); ++p) {
      const auto& port = s.ports[p];
      // Skip fully-quiet ports (down, no errors) to keep the output focused on wired ports.
      if (!port.link_up && port.invalid_frames == 0 && port.rx_errors == 0 && port.lost_links == 0) {
        continue;
      }
      os << "          port " << p << ": " << (port.link_up ? "up  " : "down") << "  invalid=" << port.invalid_frames
         << "  rx_err=" << port.rx_errors << "  lost_links=" << port.lost_links << "\n";
    }

    std::string active_ports_list;
    for (std::size_t p = 0; p < s.topology.active_ports.size(); ++p) {
      if (s.topology.active_ports[p]) {
        active_ports_list += (active_ports_list.empty() ? "" : ",") + std::to_string(p);
      }
    }
    os << "        topology: parent=" << s.topology.parent << " (port " << static_cast<int>(s.topology.parent_port)
       << " -> entry port " << static_cast<int>(s.topology.entry_port) << ")"
       << "  active_ports=[" << active_ports_list << "]"
       << "  propagation_delay=" << s.topology.propagation_delay_ns << " ns\n";

    os << "        dc chain: next=" << s.topology.dc_next << "  previous=" << s.topology.dc_previous << "\n";
    const bool any_dc_receive_time =
        std::any_of(s.topology.dc_port_receive_time_ns.begin(), s.topology.dc_port_receive_time_ns.end(),
                    [](int32_t t) { return t != 0; });
    if (any_dc_receive_time) {
      os << "        dc port receive times [ns]:";
      for (std::size_t p = 0; p < s.topology.dc_port_receive_time_ns.size(); ++p) {
        os << "  port " << p << "=" << s.topology.dc_port_receive_time_ns[p];
      }
      os << "\n";
    }
  }

  // Executor (optional)
  if (snap.executor) {
    const auto& e = *snap.executor;
    os << "\n-- Executor --\n";
    os << "  spinning: " << (e.is_spinning ? "yes" : "no") << "\n";
    print_timing(os, "rt thread", e.rt_thread_stats);
    print_timing(os, "service thread", e.service_thread_stats);
  }

  return os.str();
}

std::ostream& operator<<(std::ostream& os, const DiagnosticsSnapshot& snap)
{
  return os << to_string(snap);
}
}  // namespace duatic::ecat_interface
