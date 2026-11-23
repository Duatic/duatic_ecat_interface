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

#include <algorithm>
#include <string>
#include <vector>

#include "duatic_ecat_interface/types.hpp"

namespace duatic::ecat_interface
{
// Differences between the configured and the existing network, one description each; empty when they match
inline std::vector<std::string> compare_network(const std::vector<ExpectedDevice>& expected,
                                                const std::vector<DeviceInfo>& found)
{
  std::vector<std::string> mismatches;
  if (expected.empty()) {
    return mismatches;
  }
  if (expected.size() != found.size()) {
    mismatches.push_back("expected " + std::to_string(expected.size()) + " devices, found " +
                         std::to_string(found.size()));
  }
  const auto hex = [](uint32_t value) {
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase << value;
    return os.str();
  };
  for (const auto& e : expected) {
    const auto device = std::find_if(found.begin(), found.end(), [&e](const DeviceInfo& d) { return d.id == e.id; });
    const std::string prefix = "device " + std::to_string(e.id) + ": ";
    if (device == found.end()) {
      mismatches.push_back(prefix + "not on the bus");
      continue;
    }
    if (device->vendor_id != e.vendor_id) {
      mismatches.push_back(prefix + "vendor id " + hex(device->vendor_id) + ", expected " + hex(e.vendor_id));
    }
    if (device->product_id != e.product_id) {
      mismatches.push_back(prefix + "product code " + hex(device->product_id) + ", expected " + hex(e.product_id));
    }
    if (e.revision && device->revision != *e.revision) {
      mismatches.push_back(prefix + "revision " + hex(device->revision) + ", expected " + hex(*e.revision));
    }
  }
  return mismatches;
}

}  // namespace duatic::ecat_interface
