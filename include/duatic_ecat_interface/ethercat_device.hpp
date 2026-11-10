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

#include <cstring>
#include <cassert>
#include <mutex>
#include <vector>
#include <string>

#include "duatic_ecat_interface/types.hpp"
#include "duatic_ecat_interface/exceptions.hpp"
#include "duatic_ecat_interface/object_dictionary.hpp"
#include "duatic_ecat_interface/ethercat_bus.hpp"
#include "duatic_ecat_interface/priority_inheriting_mutex.hpp"

namespace duatic::ecat_interface
{

/**
 * @brief EthercatDevice - per device access to a device on an EthercatBus
 * The get_/set_ pdo accessors work on the device's cached pdo copy, which the bus exchanges with its process image
 * (read_/write_ on the bus) in every update
 */
class EthercatDevice
{
public:
  // PDO typedefs
  using GenericRXPDO = std::vector<uint8_t>;
  using GenericTXPDO = std::vector<uint8_t>;

  // List of callback functions a user can register
  struct Hooks
  {
    using FunctionPtr = std::function<void(void)>;
    FunctionPtr on_startup;
    FunctionPtr on_pdo_configured;
    FunctionPtr on_pre_activate;
    FunctionPtr on_post_activate;
    FunctionPtr on_pre_shutdown;
    FunctionPtr on_post_shutdown;
  };

  friend class EthercatBus;

  explicit EthercatDevice(EthercatBus* bus, DeviceInfo device_info);
  ~EthercatDevice() = default;

  // While in theory we could have multiple "EthercatDevice" instances handling the same device
  // it makes sense for consistency to make sure that only one instance of a specific device exists
  // By deleting the copy ctr we can make sure that this is the case
  EthercatDevice(const EthercatDevice&) = delete;
  EthercatDevice& operator=(const EthercatDevice&) = delete;

  /**
   * @brief register_hooks - register as set of callbacks from the consuming class
   */
  void register_hooks(const Hooks& hooks)
  {
    hooks_ = hooks;
  }

  /**
   * @brief reset_hooks - de-register the registered callbacks (overwrite with null)
   */
  void reset_hooks()
  {
    hooks_ = {};
  }

  /**
   * @brief on_startup - in startup(), before the bus pdo mapping / dc will be configured (devices are in PRE_OP)
   */
  void on_startup()
  {
    if (hooks_.on_startup) {
      hooks_.on_startup();
    }
  }

  /**
   * @brief on_pre_activate - in activate(), before SAFE_OP is requested from the devices (pdo mapping and dc are
   * configured)
   */
  void on_pre_activate()
  {
    if (hooks_.on_pre_activate) {
      hooks_.on_pre_activate();
    }
  }

  /**
   * @brief on_post_activate - in activate(), after all devices reached SAFE_OP - the transition to OPERATIONAL
   * follows once the bus is spun
   */
  void on_post_activate()
  {
    if (hooks_.on_post_activate) {
      hooks_.on_post_activate();
    }
  }

  /**
   * @brief on_pre_shutdown - before bus will be shutdown
   */
  void on_pre_shutdown()
  {
    if (hooks_.on_pre_shutdown) {
      hooks_.on_pre_shutdown();
    }
  }

  /**
   * @brief on_post_shutdown - after bus will be shutdown
   */
  void on_post_shutdown()
  {
    if (hooks_.on_post_shutdown) {
      hooks_.on_post_shutdown();
    }
  }

  /**
   * @brief get_device_id - get the id of this specific device on the bus
   */
  DeviceId get_device_id() const
  {
    return device_info_.id;
  }

  /**
   * @brief get_device_name - get the name of this specific device on the bus
   * @note this is the name obtained from the device
   */
  const std::string& get_device_name() const
  {
    return device_info_.name;
  }

  /**
   * @brief get_device_info - collection of all information that the backend usually can provide about a device
   */
  const DeviceInfo& get_device_info() const
  {
    return device_info_;
  }

  /**
   * @brief on_pdo_configured - callback which gets called as soon as the pdos has been setup and configured in the
   * backend
   */
  void on_pdo_configured([[maybe_unused]] std::size_t configured_rx_pdo_size,
                         [[maybe_unused]] std::size_t configured_tx_pdo_size)
  {
    // For the generic device we need to allocate the necessary buffers
    rx_pdo_.resize(configured_rx_pdo_size, 0);
    rx_pdo_last_write_time_ = HighPrecisionClock::now();
    tx_pdo_.resize(configured_tx_pdo_size, 0);
    tx_pdo_last_read_time_ = HighPrecisionClock::now();

    pdo_initialized = true;

    if (hooks_.on_pdo_configured) {
      hooks_.on_pdo_configured();
    }
  }

  // Helper functions for performing sdo read and writes
  template <typename T>
  SDOReadValue<T> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0)
  {
    if (!bus_) {
      throw std::invalid_argument("Bus had not been initialized yet");
    }
    return bus_->sdo_read<T>(get_device_id(), index, sub_index);
  }

  template <typename T>
  SDOWriteResult sdo_write(const T value, const SDOIndex index, const SDOSubIndex sub_index = 0)
  {
    if (!bus_) {
      throw std::invalid_argument("Bus had not been initialized yet");
    }
    return bus_->sdo_write<T>(get_device_id(), value, index, sub_index);
  }

  /**
   * @brief read the full od from the device
   */
  ObjectDictionary read_od(bool full_read = false) const;

  /**
   * @brief change_device_state - try to enforce a state transition of the specified device into the specified state
   * @param blocking - if true wait for a certain amount of time (determined by the backend) for the transition to
   * finish
   * @return true in case of success
   * @note not thread safe
   */
  bool change_device_state(const EthercatDeviceState target_state, bool blocking = true);
  /**
   * @brief foe_write - perform a file write via ethercat
   * @note thread safe
   */
  FoEWriteResult foe_write(const std::string& file_name, std::span<const uint8_t> data);
  /**
   * @brief foe_read - perform a file read via ethercat
   * @note thread safe
   */
  FoEReadValue foe_read(const std::string& file_name, std::span<uint8_t> buffer);

  /**
   * @brief Access the currently configured RX PDO (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus.
   * This access the raw data which needs to be interpreted afterwards
   */
  void get_generic_rx_pdo(std::span<uint8_t> out) const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }
    if (out.size() != rx_pdo_.size()) {
      throw DeviceConfigurationError("Size mismatch");
    }
    std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
    std::memcpy(out.data(), rx_pdo_.data(), out.size());
  }

  /**
   * @brief Configure the next RX PDO to be sent to the device (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied to the bus on the next
   * update_write call
   */
  void set_generic_rx_pdo(std::span<const uint8_t> rx)
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }
    if (rx.size() != rx_pdo_.size()) {
      throw DeviceConfigurationError("RX PDO size mismatch");
    }

    std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
    std::memcpy(rx_pdo_.data(), rx.data(), rx.size());
  }

  /**
   * @brief Access the latest received TX PDO (tx == data the device sends the master receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied from the bus at the
   * update_read call
   */
  void get_generic_tx_pdo(std::span<uint8_t> out) const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access tx pdo - PDOs have not been configured yet");
    }
    if (out.size() != tx_pdo_.size()) {
      throw DeviceConfigurationError("Size mismatch");
    }
    std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
    std::memcpy(out.data(), tx_pdo_.data(), out.size());
  }

  /**
   * @brief Access the currently configured RX PDO (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus.
   */
  template <typename RXPDO>
  RXPDO get_rx_pdo() const
  {
    static_assert(std::is_trivially_copyable_v<RXPDO>);
    if (sizeof(RXPDO) != rx_pdo_.size()) {
      throw DeviceConfigurationError("RXPDO size (" + std::to_string(sizeof(RXPDO)) +
                                     ") does not match configured size (" + std::to_string(rx_pdo_.size()) + ")");
    }
    RXPDO temp;
    get_generic_rx_pdo({ reinterpret_cast<uint8_t*>(&temp), sizeof(RXPDO) });
    return temp;
  }

  /**
   * @brief Configure the next RX PDO to be sent to the device (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied to the bus on the next
   * update_write call
   */
  template <typename RXPDO>
  void set_rx_pdo(const RXPDO& rx)
  {
    static_assert(std::is_trivially_copyable_v<RXPDO>);
    if (sizeof(RXPDO) != rx_pdo_.size()) {
      throw DeviceConfigurationError("RXPDO size (" + std::to_string(sizeof(RXPDO)) +
                                     ") does not match configured size (" + std::to_string(rx_pdo_.size()) + ")");
    }
    set_generic_rx_pdo({ reinterpret_cast<const uint8_t*>(&rx), sizeof(RXPDO) });
  }

  /**
   * @brief Access the latest received TX PDO (tx == data the device sends the master receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied from the bus at the
   * update_read call
   */
  template <typename TXPDO>
  TXPDO get_tx_pdo() const
  {
    static_assert(std::is_trivially_copyable_v<TXPDO>);
    if (sizeof(TXPDO) != tx_pdo_.size()) {
      throw DeviceConfigurationError("TXPDO size (" + std::to_string(sizeof(TXPDO)) +
                                     ") does not match configured size (" + std::to_string(tx_pdo_.size()) + ")");
    }
    TXPDO temp;
    get_generic_tx_pdo({ reinterpret_cast<uint8_t*>(&temp), sizeof(TXPDO) });
    return temp;
  }

  /**
   * @brief Latest RX PDO write time stamp
   * Obtain the latest point in time when the internal rx pdo copy was written on the bus
   * @note this function is thread safe (within API usage scheme)
   */
  const HighPrecisionTimeStamp& get_last_rx_pdo_write_time() const
  {
    return rx_pdo_last_write_time_;
  }

  /**
   * @brief Latest TX PDO read time stamp
   * Obtain the latest point in the time when the internal tx pdo copy was updated from the bus
   * @note this function is thread safe (within API usage scheme)
   */
  const HighPrecisionTimeStamp& get_last_tx_pdo_read_time() const
  {
    return tx_pdo_last_read_time_;
  }

private:
  // Internal pointer to the actual bus
  EthercatBus* bus_{ nullptr };
  /**
   * @brief update_write - callback which gets called __before__ pdo data is sent over the line
   * @note do not manually call this function
   */
  void update_write(const HighPrecisionTimeStamp& tp);
  /**
   * @brief update_read - callback which gets called __after__ pdo data has been read from the line
   * @note do not manually call this function
   */
  void update_read(const HighPrecisionTimeStamp& tp);

  DeviceInfo device_info_{};
  Hooks hooks_{};

  /**
   * @brief clear_bus - clear the bus object reference
   * @note this may only be called by the bus
   */
  void clear_bus()
  {
    bus_ = nullptr;
  }

  // PDO related fields
  // Access should only be done via the get/set_generic_pdo methods
  bool pdo_initialized = false;
  GenericRXPDO rx_pdo_;
  HighPrecisionTimeStamp rx_pdo_last_write_time_;
  GenericTXPDO tx_pdo_;
  HighPrecisionTimeStamp tx_pdo_last_read_time_;
  mutable PriorityInheritingMutex pdo_update_mutex_;
};

}  // namespace duatic::ecat_interface
