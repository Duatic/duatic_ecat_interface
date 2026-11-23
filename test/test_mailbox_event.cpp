
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

#include <cstdint>

#include "duatic_ecat_interface/types.hpp"

using namespace duatic::ecat_interface;  // NOLINT(build/namespaces)

namespace
{
MailboxEvent make_event(MailboxProtocol protocol, MailboxEventKind kind, std::uint32_t code)
{
  MailboxEvent event;
  event.protocol = protocol;
  event.kind = kind;
  event.code = code;
  return event;
}
}  // namespace

TEST(MailboxEvent, MissingObjectAndSubIndexAreMissingObjects)
{
  EXPECT_TRUE(make_event(MailboxProtocol::CoE, MailboxEventKind::Abort, 0x06020000).is_missing_object());
  EXPECT_TRUE(make_event(MailboxProtocol::CoE, MailboxEventKind::Abort, 0x06090011).is_missing_object());
}

TEST(MailboxEvent, OtherEventsAreNotMissingObjects)
{
  // Unsupported access
  EXPECT_FALSE(make_event(MailboxProtocol::CoE, MailboxEventKind::Abort, 0x06010000).is_missing_object());
  EXPECT_FALSE(make_event(MailboxProtocol::CoE, MailboxEventKind::Timeout, 0x06020000).is_missing_object());
  EXPECT_FALSE(make_event(MailboxProtocol::SoE, MailboxEventKind::Abort, 0x06020000).is_missing_object());
}
