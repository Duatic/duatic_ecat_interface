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

#include <soem_vendor/ethercat.h>

namespace duatic::ecat_interface::internal::soem
{
/**
 * @brief Simply wraps the some provided context into something more usable
 */
struct EthercatContext
{
  // EtherCAT context data elements:

  // Port reference.
  ecx_portt ecat_port{};
  // List of slave data. Index 0 is reserved for the master, higher indices for the slaves.
  ec_slavet ecatSlavelist_[EC_MAXSLAVE];
  // Number of slaves found in the network.
  int ecatSlavecount_ = 0;
  // Slave group structure.
  ec_groupt ecatGrouplist_[EC_MAXGROUP];
  // Internal, reference to EEPROM cache buffer.
  uint8 ecatEsiBuf_[EC_MAXEEPBUF];
  // Internal, reference to EEPROM cache map.
  uint32 ecatEsiMap_[EC_MAXEEPBITMAP];
  // Internal, reference to error list.
  ec_eringt ecatEList_{};
  // Internal, reference to processdata stack buffer info.
  ec_idxstackT ecatIdxStack_{};
  // Boolean indicating if an error is available in error stack.
  boolean ecatError_ = FALSE;
  // Reference to last DC time from slaves.
  int64 ecatDcTime_ = 0;
  // Internal, SM buffer.
  ec_SMcommtypet ecatSmCommtype_[EC_MAX_MAPT];
  // Internal, PDO assign list.
  ec_PDOassignt ecatPdoAssign_[EC_MAX_MAPT];
  // Internal, PDO description list.
  ec_PDOdesct ecatPdoDesc_[EC_MAX_MAPT];
  // Internal, SM list from EEPROM.
  ec_eepromSMt ecatSm_{};
  // Internal, FMMU list from EEPROM.
  ec_eepromFMMUt ecatFmmu_{};

  // EtherCAT context data.
  ecx_contextt context = { &ecat_port,
                           &ecatSlavelist_[0],
                           &ecatSlavecount_,
                           EC_MAXSLAVE,
                           &ecatGrouplist_[0],
                           EC_MAXGROUP,
                           &ecatEsiBuf_[0],
                           &ecatEsiMap_[0],
                           0,
                           &ecatEList_,
                           &ecatIdxStack_,
                           &ecatError_,
                           0,
                           0,
                           &ecatDcTime_,
                           &ecatSmCommtype_[0],
                           &ecatPdoAssign_[0],
                           &ecatPdoDesc_[0],
                           &ecatSm_,
                           &ecatFmmu_,
                           nullptr,
                           nullptr,
                           0 };
};
}  // namespace duatic::ecat_interface::internal::soem
