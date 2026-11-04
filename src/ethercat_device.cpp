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

#include "duatic_ecat_interface/ethercat_device.hpp"
#include "duatic_ecat_interface/ethercat_bus.hpp"

namespace duatic::ecat_interface
{
EthercatDevice::EthercatDevice(EthercatBus* bus, DeviceInfo device_info) : bus_(bus), device_info_(device_info)
{
}

ObjectDictionary EthercatDevice::read_od(bool full_read) const
{
  if (!bus_) {
    throw std::invalid_argument("Bus had not been initialized yet");
  }
  return bus_->read_od(get_device_id(), full_read);
}

bool EthercatDevice::change_device_state(const EthercatDeviceState target_state, bool blocking)
{
  if (!bus_) {
    throw std::invalid_argument("Bus had not been initialized yet");
  }
  return bus_->change_device_state(get_device_id(), target_state, blocking);
}

FoEWriteResult EthercatDevice::foe_write(const std::string& file_name, std::span<const uint8_t> data)
{
  if (!bus_) {
    throw std::invalid_argument("Bus had not been initialized yet");
  }
  return bus_->foe_write(get_device_id(), file_name, data);
}

FoEReadValue EthercatDevice::foe_read(const std::string& file_name, std::span<uint8_t> buffer)
{
  if (!bus_) {
    throw std::invalid_argument("Bus had not been initialized yet");
  }
  return bus_->foe_read(get_device_id(), file_name, buffer);
}

void EthercatDevice::update_write(const HighPrecisionTimeStamp& tp)
{
  std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
  bus_->write_rx_pdo(get_device_id(), rx_pdo_);
  rx_pdo_last_write_time_ = tp;
}

void EthercatDevice::update_read(const HighPrecisionTimeStamp& tp)
{
  std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
  bus_->read_tx_pdo(get_device_id(), tx_pdo_);
  tx_pdo_last_read_time_ = tp;
};

}  // namespace duatic::ecat_interface
