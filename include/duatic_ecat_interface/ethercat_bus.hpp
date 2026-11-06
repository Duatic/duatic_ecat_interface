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
#include <memory>
#include <vector>
#include <cstdint>
#include <chrono>
#include <string>

#include <duatic_message_logger/log.hpp>

#include "duatic_ecat_interface/object_dictionary.hpp"
#include "duatic_ecat_interface/types.hpp"
#include "duatic_ecat_interface/bus_diagnostics.hpp"
#include "duatic_ecat_interface/backend.hpp"

namespace duatic::ecat_interface
{
// Forward declaration of the device class
class EthercatDevice;
// Typedef for the DevicePtr as we might want to swap to a strong ownership enforcing model in the future
using EthercatDevicePtr = std::shared_ptr<EthercatDevice>;

/**
 * @brief EthercatBus - Implementation of an EthercatMaster around any existing SDK
 * @note all non-rt critical function can throw
 * @note All functions that are marked as "not thread safe" may not be called from multiple different threads at the
 * same time
 *  @par Thread-safety summary
 * Function                                          | Thread safety
 * --------------------------------------------------| -------------------------------------------
 * initialize()                                      | not thread safe
 * scan()                                            | not thread safe
 * scan(device_id)                                   | thread safe
 * read_od(device_id, full_read)                     | not thread safe
 * startup()                                         | not thread safe
 * activate()                                        | not thread safe
 * shutdown()                                        | not thread safe
 * update_rt()                                       | thread safe
 * update_service()                                  | thread safe
 * get_parameters()                                  | thread safe
 * has_device(device_id)                             | thread safe
 * has_device_on_bus(device_id)                      | thread safe
 * read_sdo_untyped(...) (blocking)                  | thread safe
 * write_sdo_untyped(...) (blocking)                 | thread safe
 * read_sdo_untyped(..., cb) (non-blocking)          | thread safe
 * write_sdo_untyped(..., cb) (non-blocking)         | thread safe
 * sdo_read<T>(...)                                  | thread safe
 * sdo_write<T>(...)                                 | thread safe
 * read_rx_pdo(device_id)                            | thread safe
 * write_rx_pdo(device_id, data)                     | thread safe
 * read_tx_pdo(device_id, data)                      | thread safe
 * change_device_state(...)                          | not thread safe
 * foe_write(...)                                    | thread safe
 * foe_read(...)                                     | thread safe
 * read_register_untyped(...)                        | thread safe
 * write_register_untyped(...)                       | thread safe
 * register_read<T>(...)                             | thread safe
 * register_write<T>(...)                            | thread safe
 * read_eeprom_untyped(...)                          | thread safe
 * diagnostics(force_update = false)                 | thread safe
 * diagnostics(force_update = true)                  | not thread safe
 * pop_mailbox_events()                              | thread safe
 * list_interfaces() (static)                        | not thread safe
 * used_backend() (static)                           | thread safe
 */
class EthercatBus final
{
public:
  struct Parameters
  {
    // Name of the ethernet interface
    std::string interface;
    std::size_t pdo_buffer_size{ 8192 };
    // Enable or disable symmetrical transfers
    bool block_LRW{ true };

    // distributed clock configuration
    bool dc_enabled{ false };
    std::chrono::nanoseconds dc_cycle_time{};
    std::chrono::nanoseconds dc_sync0_shift{};
    std::chrono::nanoseconds master_send_offset{};

    // State transition timeouts, named after the ESI's StateMachine/Timeout elements (ETG.2000) they correspond to
    // SafeopOpTimeout: SafeOp -> Op and PreOp -> SafeOp, the DuaDrive's value. 0 disables the SafeOp -> Op timeout
    std::chrono::milliseconds timeout_transition_operational{ 9000 };
    // PreopTimeout: Init -> PreOp/Boot
    std::chrono::milliseconds timeout_transition_preop{ 3000 };
    // BackToInitTimeout: any state -> Init and Op/SafeOp -> PreOp
    std::chrono::milliseconds timeout_transition_back_to_init{ 5000 };
    // BackToSafeopTimeout: Op -> SafeOp
    std::chrono::milliseconds timeout_transition_back_to_safeop{ 200 };

    // Devices the network has to consist of, compared during initialize(); empty accepts any network
    std::vector<ExpectedDevice> expected_devices{};

    // Configure various diagnostic options related to the bus
    DiagnosticsOptions diagnostics{};
  };

  explicit EthercatBus(const Parameters& params);
  ~EthercatBus();

  /**
   * @brief initialize the bus
   * @throws BackendError
   * @return Amount of slaves found on the bus
   * @note not thread safe
   */
  int initialize();

  /**
   * @brief acquire_device - obtain a reference to the underlaying ethercat device
   * @return shared ptr to the device
   * @note not thread safe
   */
  EthercatDevicePtr acquire_device(const DeviceId id);

  /**
   * @brief scan - perform a full scan of the configured bus and return a list of all found devices
   * @note not thread safe
   */
  std::vector<DeviceInfo> scan();
  /**
   * @brief scan - check if a specific device is on the bus and provide the full information about that bus
   * @throws DeviceNotFound in case device was not found on the bus
   * @note thread safe
   */
  DeviceInfo scan(const DeviceId device_id);
  /**
   * @brief read_od - try to read the full object dictionary from the specified device
   * @param device_id - bus id of the device to read the od from
   * @param full_read - if specified yes also all subindices are read
   * @throws DeviceNotFound in case device was not found on the bus
   * @note not thread safe
   */
  ObjectDictionary read_od(const DeviceId device_id, bool full_read = false);

  /**
   * @brief startup - next step after initialize
   * This configures the PDO mapping of all devices on the bus
   * @note afterwards no additional device can be added
   * @note not thread safe
   */
  void startup();
  /**
   * @brief activate - next step after startup
   * This brings all devices into SAFE_OP mode
   * @note not thread safe
   */
  void activate();
  /**
   * @brief shutdown - perform a safe shutdown of the bus
   * @note not thread safe
   */
  void shutdown();

  /**
   * @brief update_rt - perform a single bus update step
   * @note thread safe
   * @return correction factor for update rate in case DC sync is active
   */
  std::optional<std::chrono::nanoseconds> update_rt();
  /**
   * @brief update_service - perform a single bus update step of the servicing thread
   * @note thread safe
   * @note both update_service and update_rt need to be called on a regular base
   */
  bool update_service();

  /**
   * @brief get_parameters - obtain the configuration objects
   * @return const reference to used Parameters
   * @note thread safe
   */
  const Parameters& get_parameters() const;

  /**
   * @brief Check if the bus manages a specific device
   * @param device_id - the id (bus address) of the device you are looking for
   * @return true in case the bus manages that specific device
   * @note this checks if the device is _managed_ by this bus instance object (has been added via attach device)
   * @note thread safe
   */
  bool has_device(const DeviceId device_id) const;

  /**
   * @brief Check if the specific device is found on the bus
   * @param device_id - the id (bus address) of the device you are looking for
   * @return true in case the specific device was found on the bus
   * @note this checks if the device is on the physical bus. Not if it is managed by this bus instance object
   * @note thread safe
   */
  bool has_device_on_bus(const DeviceId device_id) const;

  // Untyped functions for reading writing SDOs
  // Blocking
  /**
   * @brief read_sdo_untyped - perfrom a blocking read for the specified sdo into the given buffer
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note you can also read from devices which have not been added via "attach_device"
   */
  SDOReadResult read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                 const SDOSubIndex sub_index, bool check_size = true);
  /**
   * @brief write_sdo_untyped - pefrom a blocking write to the specified sdo from the given buffer
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note you can also write to devices which have not been added via "attach_device"
   */
  SDOWriteResult write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                   const SDOSubIndex sub_index);
  // Non-blocking
  /**
   * @brief read_sdo_untyped - perform a nonblock read to the specified sdo to the given buffer. On finish the callback
   * will be called
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note This callback may be called from a different thread than the one you used for
   * calling this function
   */
  void read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                        const SDOSubIndex sub_index, const SDOReadCallback& cb, bool check_size = true);
  /**
   * @brief write_sdo_untyped - perform a nonblock write to the specified sdo to the given buffer. On finish the
   * callback will be called
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note This callback may be called from a different thread than the one you used for
   * calling this function
   */
  void write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                         const SDOSubIndex sub_index, const SDOWriteCallback& cb);

  template <SdoValueType T>
  SDOReadValue<T> sdo_read(const DeviceId device_id, const SDOIndex index, const SDOSubIndex sub_index = 0);

  template <SdoValueType T>
  SDOWriteResult sdo_write(const DeviceId device_id, const T value, const SDOIndex index,
                           const SDOSubIndex sub_index = 0);

  /**
   * @brief read_rx_pdo - obtain raw pdo data of the rx pdo (rx -> direction the device receives)
   * @note thread safe
   */
  std::vector<uint8_t> read_rx_pdo(const DeviceId device_id) const;
  /**
   * @brief write_rx_pdo - write raw data of the rx pdo (rx -> direction the device receives)
   * @note thread safe
   */
  void write_rx_pdo(const DeviceId device_id, const std::vector<uint8_t>& data);
  /**
   * @brief read_tx_pdo - read raw data of the tx pdo (tx -> direction the device transmits)
   * @note thread safe
   */
  void read_tx_pdo(const DeviceId device_id, std::span<uint8_t> data) const;

  /**
   * @brief change_device_state - try to start a state transition of the specified device into the specified state
   * @param blocking - if true wait for a certain amount of time (determined by the backend) for the transistion to
   * finish
   * @return true in case of success
   * @note not thread safe
   */
  bool change_device_state(const DeviceId device_id, const EthercatDeviceState target_state, bool blocking = true);
  /**
   * @brief foe_write - perform a file write via ethercat
   * @note thread safe
   */
  FoEWriteResult foe_write(const DeviceId device_id, const std::string& file_name, std::span<const uint8_t> data);
  /**
   * @brief foe_read - perform a file read via ethercat
   * @note thread safe
   */
  FoEReadValue foe_read(const DeviceId device_id, const std::string& file_name, std::span<uint8_t> buffer);

  /**
   * @brief read_register_untyped - perform a blocking read to the specified ESC register
   * @note thread safe
   */
  RegisterReadResult read_register_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                           const RegisterAddress address, bool check_size = true);
  /**
   * @brief write_register_untyped - perform a blocking read to the specified ESC register
   * @note thread safe
   */
  RegisterWriteResult write_register_untyped(std::span<const uint8_t> data, const DeviceId device_id,
                                             const RegisterAddress address);

  template <EthercatValueType T>
  RegisterReadValue<T> register_read(const DeviceId device_id, const RegisterAddress address, bool check_size = true);
  template <EthercatValueType T>
  bool register_write(const DeviceId device_id, const RegisterAddress address, const T data);

  /**
   * @brief read_eeprom_untyped - perform a blocking read of the device's EEPROM (SII), starting at a word address
   * Fails while the device's firmware accesses the EEPROM. data.size() must be even
   * @note thread safe
   */
  EepromReadResult read_eeprom_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                       const EepromWordAddress word_address);

  /**
   * @brief diagnostics - obtain a diagnostics snapshot of the current bus state
   * These are mostly accumulated diagnostics data
   * @param force_update - instead of obtaining a passive diagnostics snapshop actively read diagnostics now
   * @note only threadsafe if force_update = false
   */
  DiagnosticsSnapshot diagnostics(bool force_update = false);

  /**
   * @brief pop_mailbox_events - obtain the mailbox events no access was attributed to (mostly emergencies)
   * @return the events since the last call, oldest first and at most 64 - older ones are counted in
   * MailboxStatus::mailbox_events_dropped
   * @note thread safe
   */
  std::vector<MailboxEvent> pop_mailbox_events();

  /**
   * @brief list_interface - provide a list with all supported interface names
   * @return list of strings of the interface names
   * @note thread safe
   */
  static std::vector<std::string> list_interfaces();
  /**
   * @brief used_backend - obtain the configured and used backend implementation identifier
   * @note this is mostly for debugging and identification purpouses
   * @note thread safe
   */
  static Backend used_backend();

private:
  // Pimpl pattern which hides the actual backend implementation
  class BackendImpl;
  std::unique_ptr<BackendImpl> impl_;
  logging::Logger logger_;

  // Dispatch functions we keep here in order to have a better access modell in the EthercatDevice classes
  void dispatch_device_update_write(EthercatDevice& device, const HighPrecisionTimeStamp& tp);
  void dispatch_device_update_read(EthercatDevice& device, const HighPrecisionTimeStamp& tp);
};

// Explicit specialization
template <>
SDOReadValue<std::string> EthercatBus::sdo_read<std::string>(DeviceId device_id, SDOIndex index, SDOSubIndex sub_index);
template <>
SDOWriteResult EthercatBus::sdo_write<std::string>(DeviceId device_id, const std::string data, SDOIndex index,
                                                   SDOSubIndex sub_index);

}  // namespace duatic::ecat_interface
