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
#include "duatic_ecat_interface/internal/soem/soem_context.hpp"
#include "duatic_ecat_interface/types.hpp"

namespace duatic::ecat_interface::internal::soem
{
/// Describes the mailbox access currently in flight, so events popped from
/// SOEM's error list can be attributed to it. Internal to the SOEM backend.
struct MailboxAccess
{
  DeviceId device_id{ 0 };
  MailboxProtocol protocol{ MailboxProtocol::Unknown };
  std::optional<SDOIndex> index{};
  std::optional<SDOSubIndex> sub_index{};
};

static MailboxEvent make_mailbox_event(const ec_errort& e)
{
  MailboxEvent ev{};
  ev.timestamp = HighPrecisionClock::now();
  ev.device_id = static_cast<DeviceId>(e.Slave);
  ev.severity = MailboxEventSeverity::Error;

  switch (e.Etype) {
    case EC_ERR_TYPE_SDO_ERROR:
    case EC_ERR_TYPE_SDOINFO_ERROR:
      ev.protocol = MailboxProtocol::CoE;
      ev.kind = MailboxEventKind::Abort;
      ev.code = static_cast<uint32_t>(e.AbortCode);
      ev.index = static_cast<SDOIndex>(e.Index);
      ev.sub_index = static_cast<SDOSubIndex>(e.SubIdx);
      ev.description = ec_sdoerror2string(static_cast<uint32>(e.AbortCode));
      break;

    case EC_ERR_TYPE_EMERGENCY:
    {
      EmergencyInfo emcy{};
      emcy.code = e.ErrorCode;
      emcy.error_register = e.ErrorReg;
      emcy.data = { e.b1, static_cast<uint8_t>(e.w1 & 0xFF), static_cast<uint8_t>(e.w1 >> 8),
                    static_cast<uint8_t>(e.w2 & 0xFF), static_cast<uint8_t>(e.w2 >> 8) };
      ev.protocol = MailboxProtocol::CoE;
      ev.kind = MailboxEventKind::Emergency;
      ev.code = e.ErrorCode;
      ev.severity = emcy.is_reset() ? MailboxEventSeverity::Info : MailboxEventSeverity::Error;
      ev.description = emcy.is_reset() ? "Emergency cleared" : "Device emergency";
      ev.emergency = emcy;
      break;
    }
    case EC_ERR_TYPE_SOE_ERROR:
      ev.protocol = MailboxProtocol::SoE;
      ev.kind = MailboxEventKind::Abort;
      ev.code = e.ErrorCode;
      ev.index = static_cast<SDOIndex>(e.Index);
      ev.sub_index = static_cast<SDOSubIndex>(e.SubIdx);
      ev.description = ec_soeerror2string(e.ErrorCode);
      break;

    case EC_ERR_TYPE_MBX_ERROR:
      ev.kind = MailboxEventKind::MailboxLayerError;
      ev.code = e.ErrorCode;
      ev.description = ec_mbxerror2string(e.ErrorCode);
      break;

    case EC_ERR_TYPE_PACKET_ERROR:
      ev.kind = MailboxEventKind::ProtocolViolation;
      ev.code = e.ErrorCode;
      ev.description = "Malformed mailbox packet";
      break;
      // FoE reports its failures through the return value of ecx_FOE{read,write}, never
    // through the error list - see make_foe_event(). These cases exist only to satisfy
    // -Wswitch-enum; reaching them means SOEM changed behaviour.
    case EC_ERR_TYPE_FOE_ERROR:
    case EC_ERR_TYPE_FOE_BUF2SMALL:
    case EC_ERR_TYPE_FOE_PACKETNUMBER:
    case EC_ERR_TYPE_FOE_FILE_NOTFOUND:
      ev.protocol = MailboxProtocol::FoE;
      ev.kind = MailboxEventKind::Unknown;
      ev.description =
          "Unexpected FoE error on the error list (type " + std::to_string(static_cast<int>(e.Etype)) + ")";
      break;
    case EC_ERR_TYPE_EOE_INVALID_RX_DATA:
      ev.protocol = MailboxProtocol::EoE;
      ev.kind = MailboxEventKind::Unknown;
      ev.description =
          "Unexpected EoE error on the error list (type " + std::to_string(static_cast<int>(e.Etype)) + ")";
      break;

    default:
      ev.kind = MailboxEventKind::Unknown;
      ev.description = "Unknown SOEM error type (" + std::to_string(static_cast<int>(e.Etype)) + ")";
      break;
  }
  return ev;
}

static MailboxEvent make_timeout_event(const MailboxAccess& acc)
{
  MailboxEvent ev{};
  ev.timestamp = HighPrecisionClock::now();
  ev.device_id = acc.device_id;
  ev.protocol = acc.protocol;
  ev.kind = MailboxEventKind::Timeout;
  ev.index = acc.index;
  ev.sub_index = acc.sub_index;
  ev.severity = MailboxEventSeverity::Error;
  ev.description = "No mailbox response (working counter <= 0)";
  return ev;
}

static MailboxEvent make_foe_event(const DeviceId device_id, const int wkc)
{
  MailboxEvent ev{};
  ev.timestamp = HighPrecisionClock::now();
  ev.device_id = device_id;
  ev.protocol = MailboxProtocol::FoE;
  ev.severity = MailboxEventSeverity::Error;

  // NOTE: wkc == 0 must be handled before negating - EC_ERR_TYPE_SDO_ERROR is 0.
  if (wkc == 0) {
    ev.kind = MailboxEventKind::Timeout;
    ev.description = "No FoE response (working counter 0)";
    return ev;
  }

  ev.code = static_cast<uint32_t>(-wkc);
  switch (-wkc) {
    case EC_ERR_TYPE_FOE_FILE_NOTFOUND:
      ev.kind = MailboxEventKind::NotFound;
      ev.description = "FoE file not found";
      break;
    case EC_ERR_TYPE_FOE_BUF2SMALL:
      ev.kind = MailboxEventKind::BufferTooSmall;
      ev.description = "FoE receive buffer too small";
      break;
    case EC_ERR_TYPE_FOE_PACKETNUMBER:
      ev.kind = MailboxEventKind::ProtocolViolation;
      ev.description = "FoE packet number mismatch";
      break;
    case EC_ERR_TYPE_PACKET_ERROR:
      ev.kind = MailboxEventKind::ProtocolViolation;
      ev.description = "Unexpected frame during FoE transfer";
      break;
    case EC_ERR_TYPE_FOE_ERROR:
      ev.kind = MailboxEventKind::Abort;
      ev.description = "FoE error reported by device";
      break;
    case EC_ERR_TYPE_EOE_INVALID_RX_DATA:
      ev.protocol = MailboxProtocol::EoE;
      ev.kind = MailboxEventKind::Unknown;
      ev.description = "EoE reported by device";
      break;

    default:
      ev.kind = MailboxEventKind::Unknown;
      ev.description = "FoE failed with code " + std::to_string(wkc);
      break;
  }
  return ev;
}
}  // namespace duatic::ecat_interface::internal::soem
