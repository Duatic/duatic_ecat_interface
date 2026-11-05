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

#include "duatic_ecat_interface/types.hpp"

using namespace duatic::ecat_interface;  // NOLINT(build/namespaces)

namespace
{
DeviceInfo device(DeviceId id, uint32_t vendor_id, uint32_t product_id, uint32_t revision)
{
  return DeviceInfo{
    .id = id, .name = "", .vendor_id = vendor_id, .product_id = product_id, .revision = revision, .has_dc = false
  };
}
}  // namespace

TEST(CompareNetwork, EmptyConfigurationAcceptsAnyNetwork)
{
  EXPECT_TRUE(compare_network({}, { device(1, 0x1, 0x2, 0x3) }).empty());
}

TEST(CompareNetwork, MatchingNetwork)
{
  const std::vector<ExpectedDevice> expected{ { .id = 1, .vendor_id = 0x1, .product_id = 0x2, .revision = 0x3 },
                                              { .id = 2, .vendor_id = 0x1, .product_id = 0x5 } };
  EXPECT_TRUE(compare_network(expected, { device(1, 0x1, 0x2, 0x3), device(2, 0x1, 0x5, 0x9) }).empty());
}

TEST(CompareNetwork, ReportsEveryDifference)
{
  const std::vector<ExpectedDevice> expected{ { .id = 1, .vendor_id = 0x1, .product_id = 0x2, .revision = 0x3 },
                                              { .id = 2, .vendor_id = 0x1, .product_id = 0x5 } };
  const auto mismatches = compare_network(expected, { device(1, 0x7, 0x8, 0x4) });
  const std::vector<std::string> wanted{ "expected 2 devices, found 1", "device 1: vendor id 0x7, expected 0x1",
                                         "device 1: product code 0x8, expected 0x2",
                                         "device 1: revision 0x4, expected 0x3", "device 2: not on the bus" };
  EXPECT_EQ(mismatches, wanted);
}

TEST(CompareNetwork, ReportsUnexpectedDevices)
{
  const std::vector<ExpectedDevice> expected{ { .id = 1, .vendor_id = 0x1, .product_id = 0x2 } };
  const auto mismatches = compare_network(expected, { device(1, 0x1, 0x2, 0x0), device(2, 0x1, 0x2, 0x0) });
  ASSERT_EQ(mismatches.size(), 1U);
  EXPECT_EQ(mismatches[0], "expected 1 devices, found 2");
}
