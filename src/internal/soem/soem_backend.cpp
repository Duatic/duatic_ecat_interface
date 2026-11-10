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

#include "duatic_ecat_interface/ethercat_bus.hpp"

#include <algorithm>  // NOLINT(build/include_order)
#include <mutex>      // NOLINT(build/include_order)
#include <thread>     // NOLINT(build/include_order)
#include <atomic>     // NOLINT(build/include_order)
#include <optional>   // NOLINT(build/include_order)

#include <duatic_message_logger/log.hpp>

#include "duatic_ecat_interface/exceptions.hpp"
#include "duatic_ecat_interface/ethercat_device.hpp"
#include "duatic_ecat_interface/distributed_clock_sync.hpp"

#include "duatic_ecat_interface/object_dictionary.hpp"
#include "duatic_ecat_interface/priority_inheriting_mutex.hpp"
#include "duatic_ecat_interface/realtime_utils.hpp"

#include "duatic_ecat_interface/internal/backend_impl.hpp"
#include "duatic_ecat_interface/internal/soem/soem_context.hpp"
#include "duatic_ecat_interface/internal/soem/bus_state.hpp"
#include "duatic_ecat_interface/internal/soem/mailbox.hpp"
#include "duatic_ecat_interface/internal/nic.hpp"
#include "duatic_ecat_interface/internal/network.hpp"

// Implementation of the EthercatBus for the SOEM library
namespace duatic::ecat_interface
{

using namespace internal::soem;  // NOLINT(build/namespaces)

// Helper function to convert the ALStatusCode into a human readable string
std::string al_status_code_to_string(const ALStatusCode code)
{
  return std::string(ec_ALstatuscode2string(code));
}

struct EthercatBus::BackendImpl
{
  explicit BackendImpl(const Parameters& params, EthercatBus* owner)
    : params_(params)
    , owner_(owner)
    , logger_(logging::get_logger_with_default_sink("SOEM-Backend"))
    , dc_sync_(params.cycle_time, params_.master_send_offset)
  {
  }

  ~BackendImpl()
  {
    try {
      shutdown();
    } catch (const std::exception& ex) {
      logging::fatal() << "Exception during shutdown: " << ex.what();
    }
  }

  int initialize()
  {
    if (get_bus_state() != BusState::PreInit) {
      throw BackendError("Bus is already initialized", Backend::SOEM);
    }
    // Initialize the context - this initializes the passed ethernet interface
    if (const auto ec = ecx_init(&context_.context, params_.interface.c_str()); ec <= 0) {
      throw BackendError("Failed to open interface: " + params_.interface + " Run as root!", Backend::SOEM, ec);
    }

    // The master requests every ESM transition itself so that each one is timed from its request (ETG.2000) and
    // SafeOp is only requested once DC is configured - otherwise SOEM requests PreOp and SafeOp implicitly
    context_.context.manualstatechange = 1;

    // This discovers devices - it requests Init from all devices by a broadcast first
    const auto init_request_tp = HighPrecisionClock::now();
    const int device_count = ecx_config_init(&context_.context, FALSE);
    if (device_count <= 0) {
      throw BackendError("No devices found on the bus", Backend::SOEM, device_count);
    }

    // Needed for diagnostics
    if (latest_diagnostics_.slaves.size() != static_cast<std::size_t>(context_.ecatSlavecount_)) {
      latest_diagnostics_.slaves.resize(static_cast<std::size_t>(context_.ecatSlavecount_));
    }
    last_acknowledged_al_status_codes_.assign(static_cast<std::size_t>(context_.ecatSlavecount_) + 1, 0);

    // Topology is already fully determined by ecx_config_init() above - capture it now,
    // unconditionally (it costs nothing and is unrelated to DiagnosticsOptions). parent_port /
    // entry_port / propagation_delay_ns are not valid yet - startup()'s DC configuration pass
    // fills those in once it has run.
    for (uint16_t i = 1; i <= static_cast<uint16_t>(context_.ecatSlavecount_); i++) {
      auto& topology = latest_diagnostics_.slaves[static_cast<std::size_t>(i - 1)].topology;
      topology.parent = static_cast<DeviceId>(context_.ecatSlavelist_[i].parent);
      const auto active_ports_mask = context_.ecatSlavelist_[i].activeports;
      for (std::size_t port = 0; port < topology.active_ports.size(); ++port) {
        topology.active_ports[port] = (active_ports_mask & (1U << port)) != 0;
      }
    }

    // Print some general information about some parameters
    if (is_diagnostics_enabled(params_.diagnostics.pdo_diagnostics)) {
      logging::info(logger_) << "Bus Diagnostics is enabled - this has a low impact on the timing";
    }
    if (is_diagnostics_enabled(params_.diagnostics.esc_diagnostics)) {
      logging::info(logger_) << "ESC Diagnostics is enabled - this has a high impact on the timing";
    }
    if (is_diagnostics_enabled(params_.diagnostics.esc_port_diagnostics)) {
      logging::info(logger_) << "ESC Port Diagnostics is enabled - this has a  veryhigh impact on the timing";
    }
    if (is_mailbox_polling_enabled()) {
      logging::info(logger_) << "Mailbox polling is enabled - this has a low impact on the timing";
    }

    latest_diagnostics_.bus.link_up = nic::is_up(params_.interface);
    if (!latest_diagnostics_.bus.link_up) {
      logging::warning(logger_)
          << "NIC: "
          << params_.interface << " is not reported as online - make sure the connection properly established";
    }

    update_bus_state(BusState::Initialized);

    // ecx_config_init() does not evaluate whether the devices reached Init
    if (!wait_for_device_target_state(0, ec_state::EC_STATE_INIT,
                                      remaining(init_request_tp + params_.timeout_transition_back_to_init))) {
      shutdown();
      throw BackendError("Devices did not reach 'INIT' state - aborting initialization", Backend::SOEM);
    }
    read_device_emulation();

    // The identification is compared while the devices are in Init (ETG.1000.5 slave handler)
    if (const auto mismatches = compare_network(params_.expected_devices, scan()); !mismatches.empty()) {
      std::string description = "The network does not match the configured network:";
      for (const auto& mismatch : mismatches) {
        description += "\n  " + mismatch;
      }
      logging::error(logger_) << description;
      shutdown();
      throw BackendError(description, Backend::SOEM);
    }

    const auto preop_deadline = HighPrecisionClock::now() + params_.timeout_transition_preop;
    if (!set_device_target_state(0, ec_state::EC_STATE_PRE_OP) ||
        !wait_for_device_target_state(0, ec_state::EC_STATE_PRE_OP, remaining(preop_deadline))) {
      shutdown();
      throw BackendError("Devices did not reach 'PRE_OP' state - aborting initialization", Backend::SOEM);
    }

    // SOEM style iteration - we need to start at 1 because 0 is the master
    for (int i = 1; i < device_count + 1; i++) {
      // The constructor is private to the bus, which std::make_shared cannot reach
      devices_.emplace_back(
          std::shared_ptr<EthercatDevice>(new EthercatDevice(owner_, scan(static_cast<DeviceId>(i)))));
    }

    return device_count;
  }

  std::shared_ptr<EthercatDevice> acquire_device(const DeviceId device_id)
  {
    if (get_bus_state() != BusState::Initialized) {
      throw BackendError("You may only acquire devices in the bus state: 'Initialized'", Backend::SOEM);
    }
    if (!has_device(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }

    const auto device = std::find_if(devices_.begin(), devices_.end(),
                                     [device_id](const auto& d) { return d->get_device_id() == device_id; });

    if (device == devices_.end()) {
      throw BackendError("Backend error - inconsistent device states in list", Backend::SOEM);
    }

    return *device;
  }

  const Parameters& get_parameters() const
  {
    return params_;
  }

  SDOReadResult read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                 const SDOSubIndex sub_index = 0, bool check_size = true,
                                 const int timeout = EC_TIMEOUTRXM)
  {
    // NOTE we only report some errors as exceptions as for example working counter too low can happen also in normal
    // operation In this case simply false is returned

    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // Discard currently existing mailbox events that are unrelated to this access
    drain_mailbox_events();

    // Depending on the current bus state we need to handle SDO access differently
    // When the bus is up and running we should enqueue and SDO access into the main update thread
    // otherwise we can simply directly perform the operation
    const int requested_size = static_cast<int>(data.size());

    // Directly perform the read
    int actual_size = requested_size;
    const int wkc =
        ecx_SDOread(&context_.context, device_id, index, sub_index, FALSE, &actual_size, data.data(), timeout);

    const MailboxAccess access{ device_id, MailboxProtocol::CoE, index, sub_index };
    auto [event, failed] = finish_mailbox_access(access, wkc);

    if (failed) {
      logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                              << ") for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                              << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                              << static_cast<uint16_t>(sub_index)
                              << "): " << (event ? event->description : "unknown reason") << std::endl;
      return SDOReadResult{ .success = false,
                            .actual_size_read = static_cast<std::size_t>(std::max(actual_size, 0)),
                            .working_counter = wkc,
                            .mailbox_diagnostics = event };
    }

    if (check_size && requested_size != actual_size) {
      logging::error(logger_) << "Device id  " << device_id << ": Size mismatch (expected " << requested_size
                              << " bytes, read " << actual_size << " bytes) for reading SDO (ID: 0x"
                              << std::setfill('0') << std::setw(4) << std::hex << index << ", SID 0x"
                              << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(sub_index)
                              << ")." << std::endl;
      throw BackendError("SDORead size mismatch", Backend::SOEM, actual_size);
    }
    return SDOReadResult{ .success = true,
                          .actual_size_read = static_cast<std::size_t>(std::max(actual_size, 0)),
                          .working_counter = wkc,
                          .mailbox_diagnostics = event };
  }

  void read_sdo_untyped_async(const SDOReadCallback& cb, std::span<uint8_t> data, const DeviceId device_id,
                              const SDOIndex index, const SDOSubIndex sub_index = 0, bool check_size = true,
                              const int timeout = EC_TIMEOUTRXM)
  {
    const auto result = read_sdo_untyped(data, device_id, index, sub_index, check_size, timeout);
    cb(data, device_id, index, sub_index, result);
  }

  SDOWriteResult write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                   const SDOSubIndex sub_index = 0, const int timeout = EC_TIMEOUTRXM)
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }

    // Discard currently existing mailbox events that are unrelated to this access
    drain_mailbox_events();
    // Directly perform the write
    const int wkc = ecx_SDOwrite(&context_.context, device_id, index, sub_index, FALSE, static_cast<int>(data.size()),
                                 const_cast<uint8_t*>(data.data()), timeout);

    const MailboxAccess access{ device_id, MailboxProtocol::CoE, index, sub_index };
    auto [event, failed] = finish_mailbox_access(access, wkc);
    if (failed) {
      logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                              << ") for writing SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                              << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                              << static_cast<uint16_t>(sub_index)
                              << "): " << (event ? event->description : "unknown reason") << std::endl;

      return SDOWriteResult{ .success = false, .working_counter = wkc, .mailbox_diagnostics = event };
    }
    return SDOWriteResult{ .success = true, .working_counter = wkc, .mailbox_diagnostics = event };
  }

  void write_sdo_untyped_async(const SDOWriteCallback& cb, std::span<const uint8_t> data, const DeviceId device_id,
                               const SDOIndex index, const SDOSubIndex sub_index = 0, const int timeout = EC_TIMEOUTRXM)
  {
    const auto result = write_sdo_untyped(data, device_id, index, sub_index, timeout);
    cb(device_id, index, sub_index, result);
  }

  int get_device_count() const
  {
    return context_.ecatSlavecount_;
  }

  std::vector<std::shared_ptr<EthercatDevice>> get_devices() const
  {
    return devices_;
  }

  void startup()
  {
    if (get_bus_state() != BusState::Initialized) {
      throw BackendError("Cannot perform startup - bus needs to be in 'Initialized' state", Backend::SOEM);
    }
    // Give the device the chance to prepare its pdo mapping
    for (auto& device : devices_) {
      device->on_startup();
    }

    // Disable symmetrical transfers.
    /// Needs to be done before the mapping
    if (params_.block_LRW) {
      context_.context.grouplist[0].blockLRW = 1;
    } else {
      context_.context.grouplist[0].blockLRW = 0;
    }
    // Perform PDO setup
    // As there is no way to properly determine the pdo map size before hand we let the user
    // externally configure the buffer size that we are using
    io_map_.resize(params_.pdo_buffer_size, 0);
    // And then do the actual configuration
    const int final_iomap_size = ecx_config_map_group(&context_.context, io_map_.data(), 0);
    logging::info(logger_) << "IOMap size: " << final_iomap_size << std::endl;

    if (final_iomap_size > static_cast<int>(params_.pdo_buffer_size)) {
      shutdown();
      throw BackendError("Calculated pdo size is: " + std::to_string(final_iomap_size) +
                         " whereas the configured buffer size is: " + std::to_string(params_.pdo_buffer_size));
    }

    // Setup distributed clock
    if (!ecx_configdc(&context_.context)) {
      if (params_.dc_enabled) {
        logger_.error("No devices with DC support found on the bus");
        shutdown();
        throw BackendError("No devices wiht DC support found on the bus but DC sync is enabled");
      } else {
        logger_.info("No devices with DC support found on the bus");
      }
    }
    // ecx_configdc() has now measured each slave's DC propagation delay and port linkage
    // regardless of whether dc_enabled/hasdc is set for it - capture that unconditionally, same
    // as the topology fields captured in initialize().
    for (uint16_t i = 1; i <= static_cast<uint16_t>(context_.ecatSlavecount_); i++) {
      auto& topology = latest_diagnostics_.slaves[static_cast<std::size_t>(i - 1)].topology;
      topology.parent_port = context_.ecatSlavelist_[i].parentport;
      topology.entry_port = context_.ecatSlavelist_[i].entryport;
      topology.propagation_delay_ns = context_.ecatSlavelist_[i].pdelay;
      topology.dc_next = static_cast<DeviceId>(context_.ecatSlavelist_[i].DCnext);
      topology.dc_previous = static_cast<DeviceId>(context_.ecatSlavelist_[i].DCprevious);
      topology.dc_port_receive_time_ns = { context_.ecatSlavelist_[i].DCrtA, context_.ecatSlavelist_[i].DCrtB,
                                           context_.ecatSlavelist_[i].DCrtC, context_.ecatSlavelist_[i].DCrtD };
    }

    // Setup dc sync (NOTE we only support sync0 at the moment)
    if (params_.dc_enabled) {
      for (uint16_t i = 1; i <= static_cast<uint16_t>(context_.ecatSlavecount_); i++) {
        if (context_.ecatSlavelist_[i].hasdc) {
          ecx_dcsync0(&context_.context, i, true, static_cast<uint32_t>(params_.cycle_time.count()),
                      static_cast<int32_t>(params_.dc_sync0_shift.count()));
        }
      }
    }

    update_bus_state(BusState::Configured);

    // Notify all devices that the pdos have now been configured
    for (auto& device : devices_) {
      device->on_pdo_configured(context_.ecatSlavelist_[device->get_device_id()].Obytes,
                                context_.ecatSlavelist_[device->get_device_id()].Ibytes);
    }
  }

  void activate()
  {
    if (get_bus_state() != BusState::Configured) {
      throw BackendError("Cannot perform activate - bus needs to be in 'Configured' state", Backend::SOEM);
    }

    // Give the device the chance to perform any last steps before we bring them into safeop
    for (auto& device : devices_) {
      device->on_pre_activate();
    }

    // Reset activation timestamp (needed in case of bus restart)
    activation_start_tp_ = std::nullopt;

    // Now put all devices into safeop first (this is timewise non critical). This is the only SafeOp request: the
    // devices stay in PreOp until here, so the pdo mapping and the dc configuration are complete beforehand

    // First we request it from every slave - all requests go out together and share one deadline
    update_device_states();  // the requests acknowledge pending errors based on the current AL Status
    const auto safeop_deadline =
        HighPrecisionClock::now() + transition_timeout(params_, ec_state::EC_STATE_PRE_OP, ec_state::EC_STATE_SAFE_OP);
    for (auto& device : devices_) {
      if (!set_device_target_state(device->get_device_id(), ec_state::EC_STATE_SAFE_OP)) {
        // Shutdown should handle a safe exit in all situations and bus states
        shutdown();
        throw BackendError("Could set target state for device - aborting activation", Backend::SOEM);
      }
    }
    // Now we wait for all slaves to reach it
    for (auto& device : devices_) {
      if (!wait_for_device_target_state(device->get_device_id(), ec_state::EC_STATE_SAFE_OP,
                                        remaining(safeop_deadline))) {
        const auto device_id = device->get_device_id();
        shutdown();
        throw BackendError("Device " + std::to_string(device_id) +
                               " did not reach 'SAFE_OP' state - aborting activation",
                           Backend::SOEM);
      }
    }

    // Bus is now in activated state so the update method knows that it needs to push devices into OPERATIONAL first
    update_bus_state(BusState::Activated);

    // Give the devices the chance to perform any last steps before the operational stage starts
    for (auto& device : devices_) {
      device->on_post_activate();
    }
  }

  void shutdown()
  {
    // Bus is already shutdown - not need to do anything
    if (get_bus_state() == BusState::Shutdown) {
      return;
    }
    // The bus has not been initialized - no need to shut it down
    if (get_bus_state() == BusState::PreInit) {
      update_bus_state(BusState::Shutdown);
      return;
    }
    logging::info(logger_) << "Performing bus shutdown: " << params_.interface;
    // We only initialized the bus - just close the connection
    if (get_bus_state() == BusState::Initialized) {
      std::scoped_lock lock(pdo_update_mutex_, state_mutex_, mailbox_mutex_);
      devices_.clear();
      update_bus_state(BusState::Shutdown);

      ecx_close(&context_.context);
      return;
    }
    // Check if we are in a state that we can shutdown (this is more as a fallback to detect errors)
    if (get_bus_state() != BusState::Configured && get_bus_state() != BusState::Activated &&
        get_bus_state() != BusState::Operational) {
      logging::fatal(logger_) << "Invalid BusState in shutdown: " << get_bus_state()
                              << " - dont know what to do (Critical Error)" << std::endl;
      throw BackendError("Invalid BusState in shutdown - dont know what to do", Backend::SOEM);
    }
    // Bring it into the intermediate ShuttingDown state which is needed to enforce communication
    // in the pre-shutdown function to use the synchronous calls
    update_bus_state(BusState::ShuttingDown);
    for (auto& device : devices_) {
      device->on_pre_shutdown();
    }

    // Bring all devices into pre-op - one request per device so that devices with a pending error acknowledge it,
    // a broadcast request without Ack Flag would be rejected by them (ETG.1000.6 Table 103)
    update_device_states();
    for (const auto& device : devices_) {
      set_device_target_state(device->get_device_id(), ec_state::EC_STATE_PRE_OP);
    }
    // Note: using device id 0 waits for all devices on the bus
    wait_for_device_target_state(
        0, ec_state::EC_STATE_PRE_OP,
        transition_timeout(params_, ec_state::EC_STATE_OPERATIONAL, ec_state::EC_STATE_PRE_OP));

    {
      std::scoped_lock lock(pdo_update_mutex_, state_mutex_, mailbox_mutex_);

      ecx_close(&context_.context);
      update_bus_state(BusState::Shutdown);
    }
    for (auto& device : devices_) {
      device->on_post_shutdown();
    }
    // must be run after calling on post shutdown
    devices_.clear();
  }

  bool has_device(const DeviceId device_id) const
  {
    return std::find_if(devices_.begin(), devices_.end(),
                        [&](const auto& d) { return d->get_device_id() == device_id; }) != devices_.end();
  }

  /**
   * @brief check if the specific device has been found on the bus
   * @note this is different to "has_device" which checks if we handle a specific device
   */
  bool has_device_on_bus(const DeviceId id) const
  {
    return id > 0 && id <= get_device_count();
  }

  std::optional<std::chrono::nanoseconds> update_rt()
  {
    if (get_bus_state() != BusState::Activated && get_bus_state() != BusState::Operational) {
      throw BackendError("The bus object can only be spun in 'Activated' or 'Operational' state", Backend::SOEM);
    }
    // Perform the actual pdo read/write actions, the function return the dc_correction factor for the update rate
    return internal_pdo_update();
  }

  bool update_service()
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown ||
        get_bus_state() == BusState::ShuttingDown) {
      throw BackendError("The bus object can only be spun in 'Activated' or 'Operational' state", Backend::SOEM);
    }
    return internal_service_update();
  }

  std::vector<SDOEntry> read_od_from_device(const DeviceId device_id, bool full_read)
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Od cannot be read in state PreInit or Shutdown", Backend::SOEM);
    }
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    std::vector<SDOEntry> result;

    ec_ODlistt od_list{};

    // 1. Read Object Dictionary list (top level objects)
    if (!ecx_readODlist(&context_.context, device_id, &od_list)) {
      throw DeviceNotFound("Failed to read OD list from device", Backend::SOEM);
    }

    // 2. Iterate over objects
    for (int i = 0; i < od_list.Entries; ++i) {
      SDOEntry entry{};
      ecx_readODdescription(&context_.context, static_cast<uint16_t>(i), &od_list);
      entry.index = static_cast<SDOIndex>(od_list.Index[i]);
      entry.object_code = static_cast<SDOObjectCode>(od_list.ObjectCode[i]);
      entry.data_type = static_cast<DataType>(od_list.DataType[i]);
      entry.count_sub_indices = static_cast<std::size_t>(od_list.MaxSub[i]);
      entry.name = od_list.Name[i];

      // Always push entry even if we don't expand subindices
      result.push_back(entry);
    }

    // 3. Optional: full subindex scan
    if (full_read) {
      for (auto& obj : result) {
        // Var types do now have sub objects
        if (obj.object_code == SDOObjectCode::Var) {
          continue;
        }
        // Clear previous subentries just in case
        obj.sub_entries.clear();
        ec_OElistt oe_list{};

        // Read subindex metadata for this object
        // Pass the item number (should match the index in od_list)
        int item_index = -1;
        for (int idx = 0; idx < od_list.Entries; ++idx) {
          if (od_list.Index[idx] == static_cast<uint16_t>(obj.index)) {
            item_index = idx;
            break;
          }
        }

        if (item_index < 0 || !ecx_readOE(&context_.context, static_cast<uint16_t>(item_index), &od_list, &oe_list)) {
          continue;  // some objects may not support OE info
        }

        // IMPORTANT: OElist.Entries = number of subentries returned
        for (int s = 0; s < oe_list.Entries; ++s) {
          SDOSubEntry sub{};
          sub.index = static_cast<SDOSubIndex>(s);
          sub.name = oe_list.Name[s];
          sub.data_type = static_cast<DataType>(oe_list.DataType[s]);
          sub.size = static_cast<std::size_t>(oe_list.BitLength[s] / 8);

          obj.sub_entries.push_back(sub);
        }
      }
    }

    return result;
  }

  std::vector<DeviceInfo> scan()
  {
    std::vector<DeviceInfo> result;
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Need to initialize bus first for a scan", Backend::SOEM);
    }

    for (int i = 1; i <= context_.ecatSlavecount_; i++) {
      result.emplace_back(DeviceInfo{ .id = static_cast<DeviceId>(i),
                                      .name = std::string(context_.ecatSlavelist_[i].name),
                                      .vendor_id = context_.ecatSlavelist_[i].eep_man,
                                      .product_id = context_.ecatSlavelist_[i].eep_id,
                                      .revision = context_.ecatSlavelist_[i].eep_rev,
                                      .has_dc = static_cast<bool>(context_.ecatSlavelist_[i].hasdc) });
    }
    return result;
  }

  DeviceInfo scan(const DeviceId device_id)
  {
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Need to initialize bus first for a scan", Backend::SOEM);
    }
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Cannot scan for device - it is not on the bus", Backend::SOEM);
    }
    return DeviceInfo{ .id = static_cast<DeviceId>(device_id),
                       .name = std::string(context_.ecatSlavelist_[device_id].name),
                       .vendor_id = context_.ecatSlavelist_[device_id].eep_man,
                       .product_id = context_.ecatSlavelist_[device_id].eep_id,
                       .revision = context_.ecatSlavelist_[device_id].eep_rev,
                       .has_dc = static_cast<bool>(context_.ecatSlavelist_[device_id].hasdc) };
  }

  void read_rx_pdo(const DeviceId device_id, std::span<uint8_t> data) const
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Initialized) {
      throw BackendError("Cannot access pdo for device - pdo needs to be configured first");
    }
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Cannot access pdo for device - it is not on the bus", Backend::SOEM);
    }

    const auto size = context_.ecatSlavelist_[device_id].Obytes;
    if (data.size() != static_cast<std::size_t>(size)) {
      throw BackendError("Critical array size mismatch when reading rx pdo", Backend::SOEM);
    }

    std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
    std::memcpy(data.data(), context_.ecatSlavelist_[device_id].outputs, size);
  }

  void write_rx_pdo(const DeviceId device_id, std::span<const uint8_t> data)
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Initialized) {
      throw BackendError("Cannot access pdo for device - pdo needs to be configured first");
    }
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Cannot access pdo for device - it is not on the bus", Backend::SOEM);
    }

    const auto size = context_.ecatSlavelist_[device_id].Obytes;
    if (data.size() != size) {
      throw BackendError("PDO size mismatch - cannot assign", Backend::SOEM);
    }

    std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
    std::memcpy(context_.ecatSlavelist_[device_id].outputs, data.data(), size);
  }

  void read_tx_pdo(const DeviceId device_id, std::span<uint8_t> data) const
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Initialized) {
      throw BackendError("Cannot access pdo for device - pdo needs to be configured first");
    }
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Cannot access pdo for device - it is not on the bus", Backend::SOEM);
    }

    const auto size = context_.ecatSlavelist_[device_id].Ibytes;
    if (data.size() != static_cast<std::size_t>(size)) {
      throw BackendError("Critical array size mismatch when reading tx pdo", Backend::SOEM);
    }

    std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
    std::memcpy(data.data(), context_.ecatSlavelist_[device_id].inputs, size);
  }

  FoEWriteResult foe_write(const DeviceId device_id, const std::string& file_name, std::span<const uint8_t> data)
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Backend not initialized - cannot perform file operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }

    // Discard anything left over from an earlier access
    drain_mailbox_events();

    const int wkc = ecx_FOEwrite(&context_.context, device_id, const_cast<char*>(file_name.c_str()), 0,
                                 static_cast<int>(data.size()), const_cast<uint8_t*>(data.data()), EC_TIMEOUTRXM);

    // FoE carries no index/sub_index - the drain's optional checks skip those fields.
    const MailboxAccess access{ device_id, MailboxProtocol::FoE };
    auto [event, failed] = finish_foe_access(access, wkc);

    if (failed) {
      logging::error(logger_) << "Device id " << device_id << ": FoE write of \"" << file_name << "\" failed, wkc "
                              << wkc << ": " << (event ? event->description : "unknown reason") << std::endl;
    }

    return FoEWriteResult{ .success = !failed, .working_counter = wkc, .mailbox_diagnostics = event };
  }

  FoEReadValue foe_read(const DeviceId device_id, const std::string& file_name, std::span<uint8_t> buffer)
  {
    std::lock_guard<std::mutex> lock(mailbox_mutex_);
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Backend not initialized - cannot perform file operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }

    // Discard anything left over from an earlier access
    drain_mailbox_events();

    int actual_size = static_cast<int>(buffer.size());
    const int wkc = ecx_FOEread(&context_.context, device_id, const_cast<char*>(file_name.c_str()), 0, &actual_size,
                                buffer.data(), EC_TIMEOUTRXM);

    const MailboxAccess access{ device_id, MailboxProtocol::FoE };
    auto [event, failed] = finish_foe_access(access, wkc);

    if (failed) {
      logging::error(logger_) << "Device id " << device_id << ": FoE read of \"" << file_name << "\" failed, wkc "
                              << wkc << ": " << (event ? event->description : "unknown reason") << std::endl;
      return FoEReadValue(FoEReadResult{
          .success = false, .working_counter = wkc, .actual_size_read = 0, .mailbox_diagnostics = event });
    }

    return FoEReadValue(FoEReadResult{ .success = true,
                                       .working_counter = wkc,
                                       .actual_size_read = static_cast<std::size_t>(
                                           std::min(static_cast<std::size_t>(actual_size), buffer.size())),

                                       .mailbox_diagnostics = event },
                        std::span(buffer.data(), std::min(static_cast<std::size_t>(actual_size), buffer.size())));
  }

  bool set_device_target_state(const DeviceId device_id, ec_state target_state)
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (device_id != 0 && !has_device_on_bus(device_id)) {
      logging::warning(logger_) << "Cannot change device target state - device not on bus: " << device_id;
      return false;
    }
    // Boot uses its own mailbox (ETG.1020: AL status codes 0x0015 / 0x0016 otherwise), switched while no access runs
    if (device_id != 0 && target_state == ec_state::EC_STATE_BOOT) {
      std::lock_guard<std::mutex> mailbox_lock(mailbox_mutex_);
      if (!configure_bootstrap_mailbox(device_id)) {
        return false;
      }
    }
    if (device_id != 0 && target_state == ec_state::EC_STATE_PRE_OP) {
      std::lock_guard<std::mutex> mailbox_lock(mailbox_mutex_);
      restore_standard_mailbox(device_id);
    }
    // Broadcast requests never carry the Ack Flag
    if (device_id == 0) {
      context_.ecatSlavelist_[0].state = target_state;
      return ecx_writestate(&context_.context, 0) > 0;
    }
    // The decision is based on the AL Status of the last ecx_readstate() - callers refresh it beforehand.
    // AL Control is written directly: the Ack Flag shares its bit with the Error Flag, so the request must not be
    // stored in the slave list, which only ever holds the AL Status read from the device
    const auto& device = context_.ecatSlavelist_[device_id];
    if (has_error_indication(device.state)) {
      log_acknowledged_al_status_code(device_id, device.ALstatuscode);
    }
    return ecx_FPWRw(&context_.ecat_port, device.configadr, ECT_REG_ALCTL,
                     htoes(al_control_request(target_state, device.state, device_emulation_[device_id])),
                     EC_TIMEOUTRET3) > 0;
  }

  // Reads which devices emulate their AL Status - the ESC loads this from the SII at power-up
  void read_device_emulation()
  {
    device_emulation_.assign(static_cast<std::size_t>(context_.ecatSlavecount_) + 1, false);
    for (uint16_t i = 1; i <= static_cast<uint16_t>(context_.ecatSlavecount_); i++) {
      uint16_t pdi_control = 0;
      if (ecx_FPRD(&context_.ecat_port, context_.ecatSlavelist_[i].configadr, ECT_REG_PDICTL, sizeof(pdi_control),
                   &pdi_control, EC_TIMEOUTRET) <= 0) {
        // Without the information the device is treated as before: errors are acknowledged
        logging::warning(logger_) << "Device id " << i << ": failed to read the PDI control register";
        continue;
      }
      device_emulation_[i] = has_device_emulation(etohs(pdi_control));
      latest_diagnostics_.slaves[i - 1U].device_emulation = device_emulation_[i];
      if (device_emulation_[i]) {
        logging::info(logger_) << "Device id " << i << " emulates its AL Status - errors are not acknowledged";
      }
    }
  }

  // Logs the AL Status Code acknowledged for a device - only when it changes, as activation re-requests every cycle
  void log_acknowledged_al_status_code(const DeviceId device_id, const uint16_t al_status_code)
  {
    if (last_acknowledged_al_status_codes_[device_id] == al_status_code) {
      return;
    }
    last_acknowledged_al_status_codes_[device_id] = al_status_code;
    logging::warning(logger_) << "Device " << device_id << " reports the AL error '"
                              << al_status_code_to_string(al_status_code) << "' - acknowledging it";
  }

  bool wait_for_device_target_state(const DeviceId device_id, ec_state target_state,
                                    const std::chrono::milliseconds timeout)
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto timeout_us = static_cast<int>(std::chrono::microseconds(timeout).count());
    if (ecx_statecheck(&context_.context, device_id, target_state, timeout_us) != target_state) {
      return false;
    }
    // Reaching the requested state ends an error episode - a recurring error is logged again
    if (device_id != 0) {
      last_acknowledged_al_status_codes_[device_id] = 0;
    }
    return true;
  }

  void update_device_states()
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ecx_readstate(&context_.context);
  }

  ec_state get_device_state(const DeviceId device_id)
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // Note ecx_readstate needs to be called once before to update the state
    return static_cast<ec_state>(context_.ecatSlavelist_[device_id].state & 0x0F);
  }

  RegisterReadResult read_register_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                           const RegisterAddress address)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Backend not initialized - cannot perform register operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    if (data.size() > std::numeric_limits<uint16_t>::max()) {
      throw std::logic_error("Requested data size too large");
    }

    const uint16_t size = static_cast<uint16_t>(data.size());
    const int wkc = ecx_FPRD(&context_.ecat_port, context_.ecatSlavelist_[device_id].configadr, address, size,
                             data.data(), EC_TIMEOUTRET3);

    return RegisterReadResult{
      .success = wkc > 0,
      .working_counter = wkc,
      .actual_size_read = size,
      .data = data,
    };
  }

  RegisterWriteResult write_register_untyped(std::span<const uint8_t> data, const DeviceId device_id,
                                             const RegisterAddress address)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Backend not initialized - cannot perform register operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    if (data.size() > std::numeric_limits<uint16_t>::max()) {
      throw std::logic_error("Requested data size too large");
    }
    const uint16_t size = static_cast<uint16_t>(data.size());
    const int wkc = ecx_FPWR(&context_.ecat_port, context_.ecatSlavelist_[device_id].configadr, address, size,
                             const_cast<uint8_t*>(data.data()), EC_TIMEOUTRET3);
    return RegisterWriteResult{ .success = wkc > 0, .working_counter = wkc };
  }

  EepromReadResult read_eeprom_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                       const EepromWordAddress word_address)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Backend not initialized - cannot perform eeprom operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    if (data.size() % 2 != 0) {
      throw std::logic_error("The eeprom is read in 16 bit words, the data size must be even");
    }
    if (word_address + data.size() / 2 > 0x10000) {
      throw std::logic_error("Requested eeprom range exceeds the eeprom address space");
    }

    // Concurrent reads would interleave the command sequence on the ESC's eeprom interface registers
    std::lock_guard<std::mutex> lock(eeprom_mutex_);
    const uint16 configadr = context_.ecatSlavelist_[device_id].configadr;

    // Unlike SOEM never force the eeprom away from the device's firmware
    if (context_.ecatSlavelist_[device_id].eep_pdi) {
      uint8 pdi_access = 0;
      const int wkc =
          ecx_FPRD(&context_.ecat_port, configadr, ECT_REG_EEPCFG + 1, sizeof(pdi_access), &pdi_access, EC_TIMEOUTRET);
      if (wkc <= 0 || (pdi_access & 0x01) != 0) {
        logging::warning(logger_) << "Device id " << device_id
                                  << ": cannot read the eeprom - the device firmware is accessing it";
        return EepromReadResult{};
      }
    }
    if (ecx_eeprom2master(&context_.context, device_id) <= 0) {
      logging::error(logger_) << "Device id " << device_id << ": failed to take control of the eeprom";
      return EepromReadResult{};
    }

    std::size_t bytes_read = 0;
    bool success = true;
    while (bytes_read < data.size()) {
      const auto word = static_cast<uint16>(word_address + bytes_read / 2);
      const std::size_t chunk_read = read_eeprom_chunk(data.subspan(bytes_read), configadr, word);
      if (chunk_read == 0) {
        success = false;
        break;
      }
      bytes_read += chunk_read;
    }

    // Hand the eeprom back to the PDI, where the configuration left it
    if (ecx_eeprom2pdi(&context_.context, device_id) <= 0) {
      logging::error(logger_) << "Device id " << device_id << ": failed to hand the eeprom back to the device";
      success = false;
    }

    return EepromReadResult{ .success = success, .actual_size_read = bytes_read, .data = data.first(bytes_read) };
  }

  DiagnosticsSnapshot diagnostics(bool force_update)
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      throw BackendError("Cannot access 'BusDiagnostics' on a not initialized bus", Backend::SOEM);
    }
    // We allow in non operational bus state to directly obtain device and bus diagnostics
    // This is definitely not the prefered way but necessary for some applications
    if (force_update && (get_bus_state() == BusState::Operational || get_bus_state() == BusState::Activated ||
                         get_bus_state() == BusState::Configured)) {
      throw BackendError("Bus diagnostics force update may not be used when the bus is either in 'Operational' "
                         "or 'Activated' state !",
                         Backend::SOEM);
    }

    DiagnosticsSnapshot snapshot;
    if (force_update) {
      update_diagnostics_fast(0, 0);
      update_diagnostics_slow();
      snapshot = latest_diagnostics_;
    } else {
      // The RT thread only try_locks the diagnostics, a sleeping waiter would force its unlock into a syscall
      lock_by_polling(diagnostics_mutex_);
      std::lock_guard<std::mutex> lock(diagnostics_mutex_, std::adopt_lock);
      snapshot = latest_diagnostics_;
    }

    // mailbox_status_ is never touched by the RT thread (see report_mailbox_event()'s callers),
    // so this stays entirely off the RT path - just a tiny, separately-guarded copy.
    {
      std::lock_guard<std::mutex> lock(mailbox_status_mutex_);
      snapshot.mailbox = mailbox_status_;
    }

    return snapshot;
  }

  std::vector<MailboxEvent> pop_mailbox_events()
  {
    std::vector<MailboxEvent> queued;
    std::lock_guard<std::mutex> lock(mailbox_status_mutex_);
    queued.swap(unsolicited_events_);
    return queued;
  }

private:
  // Parameterization
  const Parameters params_;
  // Reference for dispatching several methods
  EthercatBus* owner_{ nullptr };

  // Ethercat context and bus state tracking
  EthercatContext context_;
  // Memory where SOEM will store its pdos aftwards
  std::vector<uint8_t> io_map_;
  std::atomic<BusState> state_{ BusState::PreInit };
  std::optional<HighPrecisionTimeStamp> activation_start_tp_{};
  std::optional<HighPrecisionTimeStamp> last_mailbox_poll_tp_{};

  // Device management
  std::vector<std::shared_ptr<EthercatDevice>> devices_;

  // Everything update thread related
  mutable PriorityInheritingMutex pdo_update_mutex_;
  // SDO call sync
  std::mutex mailbox_mutex_;
  MailboxStatus mailbox_status_{};
  // Events no access could be attributed to (mostly emergencies), until pop_mailbox_events() hands them out
  std::vector<MailboxEvent> unsolicited_events_;
  static constexpr std::size_t max_unsolicited_events = 64;
  mutable std::mutex mailbox_status_mutex_;
  // State change related
  std::mutex state_mutex_;
  // Per device id (index 0 unused), the AL Status Code that was acknowledged last
  std::vector<uint16_t> last_acknowledged_al_status_codes_;
  // Per device id (index 0 unused), whether the device emulates its AL Status
  std::vector<bool> device_emulation_;
  // EEPROM access
  std::mutex eeprom_mutex_;
  // Pause between two polls of the eeprom interface, SOEM's EC_LOCALDELAY which it keeps private
  static constexpr uint32 eeprom_poll_delay_us = 200;

  // Backend logger
  logging::Logger logger_;

  // DC
  DCSyncController dc_sync_;

  // Diagnostics
  DiagnosticsSnapshot latest_diagnostics_;
  std::mutex diagnostics_mutex_;
  std::size_t current_selected_diagnostics_slave_ = 0;  // note this is starting at 0 (not device id index based)

  void update_bus_state(BusState state)
  {
    logging::info(logger_) << "Bus transitioning into state: " << state;
    state_ = state;
  }

  BusState get_bus_state() const
  {
    return state_;
  }

  std::optional<std::chrono::nanoseconds> internal_pdo_update()
  {
    std::optional<std::chrono::nanoseconds> update_rate_correction_factor{ std::nullopt };
    // Obtain the current timestamp we stamp on the read /write times
    const auto now = HighPrecisionClock::now();
    // Take the latest tx pdo state from every device
    for (auto& device : devices_) {
      owner_->dispatch_device_update_write(*device, now);
    }

    // Stored outside the lock block so that the diagnostics can access it
    int wkc{ 0 };
    int expected_wkc{ 0 };

    {  // Important to lock after update_write(), otherwise update_write will deadlock
      std::lock_guard<PriorityInheritingMutex> lock(pdo_update_mutex_);
      if (ecx_send_processdata(&context_.context) <= 0) {
        logging::error(logger_) << "Failed to send process data" << std::endl;
      }
      wkc = ecx_receive_processdata(&context_.context, EC_TIMEOUTRET);

      expected_wkc = context_.context.grouplist[0].outputsWKC * 2 + context_.context.grouplist[0].inputsWKC;
      if (wkc < expected_wkc) {
        logging::warning(logger_) << params_.interface << " Working counter too low (pdo): " << wkc
                                  << " expected wkc: " << expected_wkc << std::endl;
      }
      // Run DC clock pi controller to calculate a correction factor which then can be used by executor to shift the
      // update rate
      if (params_.dc_enabled && context_.ecatSlavelist_[0].hasdc) {
        update_rate_correction_factor = dc_sync_.update(std::chrono::nanoseconds{ context_.ecatDcTime_ });
      }
    }  // Important to unlock here, otherwise update_read() will deadlock

    // Update the pdo state of every device
    for (auto& device : devices_) {
      owner_->dispatch_device_update_read(*device, now);
    }

    // On older systems already this diagnostics has a measurable impact
    if (is_diagnostics_enabled(params_.diagnostics.pdo_diagnostics)) {
      update_diagnostics_fast(wkc, expected_wkc);
    }
    return update_rate_correction_factor;
  }

  bool internal_service_update()
  {
    // Cheap (a handful of local syscalls, no bus traffic) - refresh every tick regardless of
    // DiagnosticsOptions, same as the one-time capture in initialize() this now supersedes.
    latest_diagnostics_.bus.link_up = nic::is_up(params_.interface);

    if (std::unique_lock<std::mutex> lock(mailbox_mutex_, std::try_to_lock); lock.owns_lock()) {
      poll_mailboxes();
      drain_mailbox_events();
    }

    // First thing after startup is to bring the devices into operational state
    if (get_bus_state() == BusState::Activated) {
      if (!activation_start_tp_) {
        activation_start_tp_ = HighPrecisionClock::now();
      }
      // timeout == 0 -> disable timeout
      if (params_.timeout_transition_operational > std::chrono::milliseconds{ 0 } &&
          (HighPrecisionClock::now() - activation_start_tp_.value()) > params_.timeout_transition_operational) {
        logging::fatal(logger_) << "Failed to put all devices into state 'Operational' within time. Aborting"
                                << std::endl;
        shutdown();
        throw BackendError("Could not bring all devices into state 'Operational' within the given timeframe",
                           Backend::SOEM);
      }

      // Update the context once (needed for the device state check and the acknowledge of pending errors)
      update_device_states();  // go through the lock

      // Only the devices that are not operational yet are (re-)requested
      bool all_in_operational = true;
      for (const auto& device : devices_) {
        if (get_device_state(device->get_device_id()) != ec_state::EC_STATE_OPERATIONAL) {
          all_in_operational = false;
          set_device_target_state(device->get_device_id(), ec_state::EC_STATE_OPERATIONAL);
        }
      }

      if (all_in_operational) {
        {
          // Reaching Operational ends the error episodes of the activation - a recurring error is logged again
          std::lock_guard<std::mutex> lock(state_mutex_);
          std::fill(last_acknowledged_al_status_codes_.begin(), last_acknowledged_al_status_codes_.end(), 0);
        }
        update_bus_state(BusState::Operational);
      }

      return true;
    } else {
      // If the bus is in operational state we can perform bus diagnostics in the background
      // but this has a timing impact
      if (is_diagnostics_enabled(params_.diagnostics.esc_diagnostics)) {
        update_device_states();
        if (is_diagnostics_enabled(params_.diagnostics.esc_port_diagnostics)) {
          update_diagnostics_slow();
        }
      }
    }
    return is_diagnostics_enabled(params_.diagnostics.mailbox_diagnostics) ||
           is_diagnostics_enabled(params_.diagnostics.esc_diagnostics) ||
           is_diagnostics_enabled(params_.diagnostics.esc_port_diagnostics);
  }

  void update_diagnostics_fast(const int wkc, const int expected_wkc)
  {
    // Update the internal diagnostics with:
    latest_diagnostics_.timestamp = HighPrecisionClock::now();

    // Case a slave did not answer
    if (wkc >= 0 && wkc != expected_wkc) {
      latest_diagnostics_.bus.wkc_mismatches += 1;
    }
    // Case a frame got lost or timeout or whatever
    if (wkc < 0) {
      latest_diagnostics_.bus.frames_lost += 1;
    }
    // Simply count up how many frames we actually sent so far (PDO only)
    latest_diagnostics_.bus.frames_sent += 1;

    // now we use the cache data we have available
    if (is_diagnostics_enabled(params_.diagnostics.esc_diagnostics)) {
      // NOTE as all accesses before are simple access on x86 we only try to lock the diagnostics mutex at this point
      // This is on some architectures a risk we accept it on purpose to improve the timing behavior of the RT loop
      std::unique_lock<std::mutex> lock(diagnostics_mutex_, std::try_to_lock);
      if (!lock.owns_lock()) {
        return;
      }
      for (uint16_t i = 0; i < static_cast<uint16_t>(context_.ecatSlavecount_); i++) {
        latest_diagnostics_.slaves[i].position = i + 1;
        const uint16_t al_status = context_.ecatSlavelist_[i + 1].state;
        latest_diagnostics_.slaves[i].al_status_code =
            valid_al_status_code(al_status, context_.ecatSlavelist_[i + 1].ALstatuscode);
        latest_diagnostics_.slaves[i].error_indication = has_error_indication(al_status);
        latest_diagnostics_.slaves[i].state = map_from_soem_device_state(static_cast<ec_state>(al_status & 0x0F));
        latest_diagnostics_.slaves[i].online = !context_.ecatSlavelist_[i + 1].islost;
      }
    }
  }

  void update_diagnostics_slow()
  {
    // in round robin
    if (current_selected_diagnostics_slave_ >= static_cast<std::size_t>(context_.ecatSlavecount_)) {
      current_selected_diagnostics_slave_ = 0;
    }
    const auto config_adr = context_.ecatSlavelist_[current_selected_diagnostics_slave_ + 1].configadr;

    // Slow (bus round-trip via ecx_FPRD): computed into locals, no lock held.
    const auto ports = read_slave_port_diagnostics(config_adr);

    // Fast (struct copy only): diagnostics_mutex_ is held just long enough to publish the
    // result, consistent with its role for the rest of latest_diagnostics_.slaves (see
    // update_diagnostics_fast()) - never held across the register reads above.
    {
      lock_by_polling(diagnostics_mutex_);
      std::lock_guard<std::mutex> lock(diagnostics_mutex_, std::adopt_lock);
      latest_diagnostics_.slaves[current_selected_diagnostics_slave_].ports = ports;
      latest_diagnostics_.slaves[current_selected_diagnostics_slave_].ports_update_timestamp =
          HighPrecisionClock::now();
    }
    current_selected_diagnostics_slave_ += 1;
  }

  std::array<ESCPortHealth, 4> read_slave_port_diagnostics(const uint16 config_adr)
  {
    constexpr uint16_t DL_STATUS = 0x0110;          // 2 bytes: link/loop/comm status
    constexpr uint16_t RX_ERROR_COUNTER = 0x0300;   // 8 bytes: 2 per port (invalid, rx err) x4
    constexpr uint16_t LOST_LINK_COUNTER = 0x0310;  // 4 bytes: 1 per port x4
    uint16_t dl_status = 0;
    ecx_FPRD(&context_.ecat_port, config_adr, DL_STATUS, sizeof(dl_status), &dl_status, EC_TIMEOUTRET);

    uint8_t rx_err[8] = {};
    ecx_FPRD(&context_.ecat_port, config_adr, RX_ERROR_COUNTER, sizeof(rx_err), rx_err, EC_TIMEOUTRET);

    uint8_t lost_link[4] = {};
    ecx_FPRD(&context_.ecat_port, config_adr, LOST_LINK_COUNTER, sizeof(lost_link), lost_link, EC_TIMEOUTRET);

    std::array<ESCPortHealth, 4> ports{};
    for (std::size_t p = 0; p < 4; ++p) {
      // Physical link bits per port sit at bits 4-7 of DL Status.
      // VERIFY bit layout against your ESC datasheet.
      ports[p].link_up = (dl_status & (1u << (4 + p))) != 0;
      ports[p].invalid_frames = rx_err[p * 2];
      ports[p].rx_errors = rx_err[p * 2 + 1];
      ports[p].lost_links = lost_link[p];
    }
    return ports;
  }

  /// Empties SOEM's error list. The first event attributable to `acc` is returned,
  /// everything else is logged and counted. Call with mailbox_mutex_ held.
  std::optional<MailboxEvent> drain_mailbox_events(const MailboxAccess& acc)
  {
    std::optional<MailboxEvent> attributed{};
    ec_errort raw{};
    while (ecx_poperror(&context_.context, &raw)) {
      MailboxEvent ev = make_mailbox_event(raw);

      // An event belongs to the in-flight access if it is solicited (emergencies never
      // are), comes from the right device, and does not contradict it on any field the
      // event actually carries - FoE events carry no index/sub_index at all.
      const bool belongs_to_access = ev.kind != MailboxEventKind::Emergency && ev.device_id == acc.device_id &&
                                     (ev.protocol == MailboxProtocol::Unknown || ev.protocol == acc.protocol) &&
                                     (!ev.index || !acc.index || *ev.index == *acc.index) &&
                                     (!ev.sub_index || !acc.sub_index || *ev.sub_index == *acc.sub_index);

      if (!attributed && belongs_to_access) {
        attributed = std::move(ev);
        continue;
      }
      report_unsolicited_event(std::move(ev));
    }
    return attributed;
  }

  /// Switches the device's mailbox SyncManagers to the bootstrap mailbox of its SII (word 0x0014, ETG.2000).
  /// Call with state_mutex_ and mailbox_mutex_ held.
  bool configure_bootstrap_mailbox(const DeviceId device_id)
  {
    std::array<uint8_t, 8> sii{};
    if (!read_eeprom_untyped(sii, device_id, ECT_SII_BOOTRXMBX)) {
      logging::error(logger_) << "Device id " << device_id << ": failed to read the bootstrap mailbox from the eeprom";
      return false;
    }
    const auto word = [&sii](std::size_t i) { return static_cast<uint16>(sii[2 * i] | (sii[2 * i + 1] << 8)); };
    if (word(1) == 0 || word(3) == 0) {
      logging::error(logger_) << "Device id " << device_id << ": the device does not support the bootstrap state";
      return false;
    }
    auto& slave = context_.ecatSlavelist_[device_id];
    ec_smt rx{ .StartAddr = htoes(word(0)), .SMlength = htoes(word(1)), .SMflags = slave.SM[0].SMflags };
    ec_smt tx{ .StartAddr = htoes(word(2)), .SMlength = htoes(word(3)), .SMflags = slave.SM[1].SMflags };
    if (ecx_FPWR(&context_.ecat_port, slave.configadr, ECT_REG_SM0, sizeof(rx), &rx, EC_TIMEOUTRET) <= 0 ||
        ecx_FPWR(&context_.ecat_port, slave.configadr, ECT_REG_SM1, sizeof(tx), &tx, EC_TIMEOUTRET) <= 0) {
      logging::error(logger_) << "Device id " << device_id << ": failed to configure the bootstrap mailbox";
      return false;
    }
    slave.mbx_wo = word(0);
    slave.mbx_l = word(1);
    slave.mbx_ro = word(2);
    slave.mbx_rl = word(3);
    return true;
  }

  /// Switches the device's mailbox SyncManagers back to the standard mailbox SOEM configured, if the bootstrap mailbox
  /// was in use. Call with state_mutex_ and mailbox_mutex_ held.
  void restore_standard_mailbox(const DeviceId device_id)
  {
    auto& slave = context_.ecatSlavelist_[device_id];
    if (slave.mbx_wo == etohs(slave.SM[0].StartAddr) && slave.mbx_l == etohs(slave.SM[0].SMlength) &&
        slave.mbx_ro == etohs(slave.SM[1].StartAddr) && slave.mbx_rl == etohs(slave.SM[1].SMlength)) {
      return;
    }
    if (ecx_FPWR(&context_.ecat_port, slave.configadr, ECT_REG_SM0, sizeof(ec_smt), &slave.SM[0], EC_TIMEOUTRET) <= 0 ||
        ecx_FPWR(&context_.ecat_port, slave.configadr, ECT_REG_SM1, sizeof(ec_smt), &slave.SM[1], EC_TIMEOUTRET) <= 0) {
      // The device then refuses PreOp with AL status code 0x0016, which the state change reports
      logging::error(logger_) << "Device id " << device_id << ": failed to restore the standard mailbox";
      return;
    }
    slave.mbx_wo = etohs(slave.SM[0].StartAddr);
    slave.mbx_l = etohs(slave.SM[0].SMlength);
    slave.mbx_ro = etohs(slave.SM[1].StartAddr);
    slave.mbx_rl = etohs(slave.SM[1].SMlength);
  }

  /// Returns the eeprom status once the current command finished, nothing on a timeout
  std::optional<uint16> wait_for_eeprom_not_busy(const uint16 configadr)
  {
    osal_timert timer;
    osal_timer_start(&timer, EC_TIMEOUTEEP);
    do {
      uint16 status = 0;
      const int wkc = ecx_FPRD(&context_.ecat_port, configadr, ECT_REG_EEPSTAT, sizeof(status), &status, EC_TIMEOUTRET);
      status = etohs(status);
      if (wkc > 0 && (status & EC_ESTAT_BUSY) == 0) {
        return status;
      }
      osal_usleep(eeprom_poll_delay_us);
    } while (!osal_timer_is_expired(&timer));
    return std::nullopt;
  }

  /// Reads the 4 or 8 bytes of one eeprom read command into the front of data, checking every frame unlike
  /// ecx_readeepromFP. Returns the bytes read, 0 on failure. Call with eeprom_mutex_ held.
  std::size_t read_eeprom_chunk(std::span<uint8_t> data, const uint16 configadr, const uint16 word)
  {
    auto status = wait_for_eeprom_not_busy(configadr);
    if (!status) {
      return 0;
    }
    // Clear the error bits of an earlier command
    if ((*status & EC_ESTAT_EMASK) != 0) {
      uint16 nop = htoes(static_cast<uint16>(EC_ECMD_NOP));
      if (ecx_FPWR(&context_.ecat_port, configadr, ECT_REG_EEPCTL, sizeof(nop), &nop, EC_TIMEOUTRET) <= 0) {
        return 0;
      }
    }
    std::array<uint16, 3> command{ htoes(static_cast<uint16>(EC_ECMD_READ)), htoes(word), 0 };
    if (ecx_FPWR(&context_.ecat_port, configadr, ECT_REG_EEPCTL, sizeof(command), command.data(), EC_TIMEOUTRET) <= 0) {
      return 0;
    }
    status = wait_for_eeprom_not_busy(configadr);
    if (!status || (*status & EC_ESTAT_NACK) != 0) {
      return 0;
    }
    std::array<uint8_t, 8> raw{};
    const std::size_t raw_size = (*status & EC_ESTAT_R64) != 0 ? 8 : 4;
    if (ecx_FPRD(&context_.ecat_port, configadr, ECT_REG_EEPDAT, static_cast<uint16>(raw_size), raw.data(),
                 EC_TIMEOUTRET) <= 0) {
      return 0;
    }
    const std::size_t chunk = std::min(raw_size, data.size());
    std::copy_n(raw.begin(), chunk, data.begin());
    return chunk;
  }

  bool is_mailbox_polling_enabled() const
  {
    return is_diagnostics_enabled(params_.diagnostics.mailbox_diagnostics) &&
           params_.diagnostics.mailbox_poll_interval > std::chrono::milliseconds{ 0 };
  }

  /// Reads every input mailbox that holds data, so unsolicited messages such as emergencies
  /// reach SOEM's error list between accesses. Call with mailbox_mutex_ held.
  void poll_mailboxes()
  {
    if (!is_mailbox_polling_enabled()) {
      return;
    }
    const auto now = HighPrecisionClock::now();
    if (last_mailbox_poll_tp_ && now - *last_mailbox_poll_tp_ < params_.diagnostics.mailbox_poll_interval) {
      return;
    }
    last_mailbox_poll_tp_ = now;

    // A broadcast read ORs the SyncManager 1 status of all devices, so one frame tells whether any input mailbox is
    // full (bit 3) - the idle case costs a single frame regardless of the number of devices
    uint16 status = 0;
    const int wkc = ecx_BRD(context_.context.port, 0, ECT_REG_SM1STAT, sizeof(status), &status, EC_TIMEOUTRET);
    if (wkc <= 0 || (etohs(status) & 0x08) == 0) {
      return;
    }

    for (uint16 i = 1; i <= static_cast<uint16>(context_.ecatSlavecount_); i++) {
      if (context_.ecatSlavelist_[i].mbx_rl == 0) {
        continue;
      }
      // Timeout 0 makes ecx_mbxreceive read the status and the mailbox once: with a timeout it keeps reading the
      // emptied mailbox after an emergency, and the repeat requests that triggers deliver the emergency again
      ec_mbxbuft mbx;
      ec_clearmbx(&mbx);
      ecx_mbxreceive(&context_.context, i, &mbx, 0);
    }
  }

  /// Unattributed drain - clears stale entries so the next access starts clean,
  /// and keeps SOEM's fixed-size elist from overflowing. Call with mailbox_mutex_ held.
  void drain_mailbox_events()
  {
    ec_errort raw{};
    while (ecx_poperror(&context_.context, &raw)) {
      report_unsolicited_event(make_mailbox_event(raw));
    }
  }

  /// Reports an event nobody receives as an access result and keeps it for pop_mailbox_events()
  void report_unsolicited_event(MailboxEvent ev)
  {
    report_mailbox_event(ev);
    std::lock_guard<std::mutex> lock(mailbox_status_mutex_);
    if (unsolicited_events_.size() == max_unsolicited_events) {
      unsolicited_events_.erase(unsolicited_events_.begin());
      mailbox_status_.events_dropped += 1;
    }
    unsolicited_events_.push_back(std::move(ev));
  }

  void report_mailbox_event(const MailboxEvent& ev)
  {
    {
      std::lock_guard<std::mutex> lock(mailbox_status_mutex_);
      switch (ev.kind) {
        case MailboxEventKind::Emergency:
          mailbox_status_.emergencies += 1;
          break;
        case MailboxEventKind::Timeout:
          mailbox_status_.timeouts += 1;
          break;
        case MailboxEventKind::Unknown:
        case MailboxEventKind::Abort:
        case MailboxEventKind::ProtocolViolation:
        case MailboxEventKind::MailboxLayerError:
        case MailboxEventKind::BufferTooSmall:
        case MailboxEventKind::NotFound:
          mailbox_status_.aborts += 1;
          break;
      }
    }

    if (!is_diagnostics_enabled(params_.diagnostics.mailbox_diagnostics)) {
      return;
    }
    // Emergencies are the one kind no result struct carries, so log them at Basic;
    // the rest only under Tracing.
    const bool always_log = ev.kind == MailboxEventKind::Emergency;
    if (!always_log && params_.diagnostics.mailbox_diagnostics != DiagnosticsLevel::Tracing) {
      return;
    }

    auto&& sink = (ev.severity == MailboxEventSeverity::Info) ? logging::info(logger_) : logging::warning(logger_);
    sink << "Mailbox event (device " << ev.device_id << ", " << ev.description << ", code 0x" << std::hex << ev.code
         << std::dec << ")" << std::endl;
  }

  std::pair<std::optional<MailboxEvent>, bool> finish_mailbox_access(const MailboxAccess& acc, const int wkc)
  {
    auto event = drain_mailbox_events(acc);
    if (wkc <= 0 && !event) {
      event = make_timeout_event(acc);
      report_mailbox_event(*event);
    }
    // An attributed event is never an Emergency by construction, so severity alone decides.
    const bool failed = wkc <= 0 || (event && event->severity == MailboxEventSeverity::Error);
    return { std::move(event), failed };
  }

  std::pair<std::optional<MailboxEvent>, bool> finish_foe_access(const MailboxAccess& acc, const int wkc)
  {
    // FoE itself never pushes, but the ecx_mbxreceive underneath it does - mailbox-layer
    // errors and emergencies still arrive this way, so the drain is not optional.
    auto event = drain_mailbox_events(acc);

    if (wkc > 0) {
      return { std::move(event), event && event->severity == MailboxEventSeverity::Error };
    }
    if (event) {
      // The result carries the FoE code instead, so the event is handed out through pop_mailbox_events()
      report_unsolicited_event(std::move(*event));
    }
    // A decoded FoE code is more specific than anything the drain could have found.
    event = make_foe_event(acc.device_id, wkc);
    report_mailbox_event(*event);
    return { std::move(event), true };
  }
};

// Pimpl - redirections
EthercatBus::EthercatBus(const Parameters& params) : logger_(logging::get_logger_with_default_sink("EthercatBus"))
{
  // NOTE we pass a pointer to ourself on purpose to the backend as this is the best way to
  // dispatch calls to the actual ethercat device classes
  impl_ = std::make_unique<EthercatBus::BackendImpl>(params, this);
}

EthercatBus::~EthercatBus()
{
  for (auto& device : impl_->get_devices()) {
    device->clear_bus();
  }
}

int EthercatBus::initialize()
{
  return impl_->initialize();
}

EthercatDevicePtr EthercatBus::acquire_device(const DeviceId device_id)
{
  return impl_->acquire_device(device_id);
}

std::optional<std::chrono::nanoseconds> EthercatBus::update_rt()
{
  return impl_->update_rt();
}

bool EthercatBus::update_service()
{
  return impl_->update_service();
}

const EthercatBus::Parameters& EthercatBus::get_parameters() const
{
  return impl_->get_parameters();
}

bool EthercatBus::has_device(const DeviceId device_id) const
{
  return impl_->has_device(device_id);
}

bool EthercatBus::has_device_on_bus(const DeviceId device_id) const
{
  return impl_->has_device_on_bus(device_id);
}

SDOReadResult EthercatBus::read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                            const SDOSubIndex sub_index, bool check_size)
{
  return impl_->read_sdo_untyped(data, device_id, index, sub_index, check_size);
}

void EthercatBus::read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                   const SDOSubIndex sub_index, const SDOReadCallback& cb, bool check_size)
{
  impl_->read_sdo_untyped_async(cb, data, device_id, index, sub_index, check_size);
}

SDOWriteResult EthercatBus::write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id,
                                              const SDOIndex index, const SDOSubIndex sub_index)
{
  return impl_->write_sdo_untyped(data, device_id, index, sub_index);
}

void EthercatBus::write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                    const SDOSubIndex sub_index, const SDOWriteCallback& cb)
{
  impl_->write_sdo_untyped_async(cb, data, device_id, index, sub_index);
}

ObjectDictionary EthercatBus::read_od(const DeviceId device_id, bool full_read)
{
  return ObjectDictionary{ impl_->read_od_from_device(device_id, full_read) };
}

std::vector<DeviceInfo> EthercatBus::scan()
{
  return impl_->scan();
}

void EthercatBus::startup()
{
  impl_->startup();
}

void EthercatBus::activate()
{
  impl_->activate();
}

void EthercatBus::shutdown()
{
  impl_->shutdown();
}

std::vector<std::string> EthercatBus::list_interfaces()
{
  ec_adaptert* adapter = nullptr;

  adapter = ec_find_adapters();
  if (!adapter)
    throw BackendError("Error calling ec_find_adapters", Backend::SOEM);

  std::vector<std::string> result;

  auto iter = adapter;
  while (iter != nullptr) {
    result.push_back(iter->name);
    iter = iter->next;
  }

  ec_free_adapters(adapter);
  return result;
}

Backend EthercatBus::used_backend()
{
  return Backend::SOEM;
}

DeviceInfo EthercatBus::scan(const DeviceId device_id)
{
  return impl_->scan(device_id);
}

void EthercatBus::read_rx_pdo(const DeviceId device_id, std::span<uint8_t> data) const
{
  impl_->read_rx_pdo(device_id, data);
}

void EthercatBus::write_rx_pdo(const DeviceId device_id, std::span<const uint8_t> data)
{
  impl_->write_rx_pdo(device_id, data);
}

void EthercatBus::read_tx_pdo(const DeviceId device_id, std::span<uint8_t> data) const
{
  impl_->read_tx_pdo(device_id, data);
}

template <SDOValueType T>
SDOReadValue<T> EthercatBus::sdo_read(const DeviceId device_id, const SDOIndex index, const SDOSubIndex sub_index)
{
  static_assert(!std::is_same_v<T, std::string>);
  // This is the actual data instance we use
  T data{};
  // And this is just a safe representation (pointer + size) to it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(&data), sizeof(T));

  const auto result = read_sdo_untyped(buffer, device_id, index, sub_index);
  // Read was not successful
  if (!result) {
    return SDOReadValue<T>(result);
  }
  return SDOReadValue<T>(result, data);
}

template <>
SDOReadValue<std::string> EthercatBus::sdo_read<std::string>(const DeviceId device_id, const SDOIndex index,
                                                             const SDOSubIndex sub_index)
{
  // Create a buffer of the maximum possible string length
  std::vector<char> data(maximum_visible_string_size, 0);
  // Create a uint8_t span representation of it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(data.data()), data.size());
  const auto result =
      read_sdo_untyped(buffer, device_id, index, sub_index, false /*do not perform size check for string reads*/);
  if (!result) {
    // Something during the read failed
    return SDOReadValue<std::string>(result);
  }

  // Create an std::string out of it
  const std::string_view raw{ data.data(), std::min<std::size_t>(result.actual_size_read, data.size()) };
  const auto text = raw.substr(0, raw.find('\0'));
  return SDOReadValue<std::string>(result, std::string(text));
}

template SDOReadValue<bool> EthercatBus::sdo_read<bool>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<uint8_t> EthercatBus::sdo_read<uint8_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<int8_t> EthercatBus::sdo_read<int8_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<uint16_t> EthercatBus::sdo_read<uint16_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<int16_t> EthercatBus::sdo_read<int16_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<uint32_t> EthercatBus::sdo_read<uint32_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<int32_t> EthercatBus::sdo_read<int32_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<uint64_t> EthercatBus::sdo_read<uint64_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<int64_t> EthercatBus::sdo_read<int64_t>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<float> EthercatBus::sdo_read<float>(DeviceId, SDOIndex, SDOSubIndex);
template SDOReadValue<double> EthercatBus::sdo_read<double>(DeviceId, SDOIndex, SDOSubIndex);

template <SDOValueType T>
SDOWriteResult EthercatBus::sdo_write(const DeviceId device_id, const T value, const SDOIndex index,
                                      const SDOSubIndex sub_index)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
  return write_sdo_untyped(data, device_id, index, sub_index);
}

template <>
SDOWriteResult EthercatBus::sdo_write<std::string>(const DeviceId device_id, const std::string value,
                                                   const SDOIndex index, const SDOSubIndex sub_index)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  return write_sdo_untyped(data, device_id, index, sub_index);
}

template SDOWriteResult EthercatBus::sdo_write<bool>(DeviceId, const bool, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<uint8_t>(DeviceId, const uint8_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<int8_t>(DeviceId, const int8_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<uint16_t>(DeviceId, const uint16_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<int16_t>(DeviceId, const int16_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<uint32_t>(DeviceId, const uint32_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<int32_t>(DeviceId, const int32_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<uint64_t>(DeviceId, const uint64_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<int64_t>(DeviceId, const int64_t, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<float>(DeviceId, const float, SDOIndex, SDOSubIndex);
template SDOWriteResult EthercatBus::sdo_write<double>(DeviceId, const double, SDOIndex, SDOSubIndex);

FoEWriteResult EthercatBus::foe_write(const DeviceId device_id, const std::string& file_name,
                                      std::span<const uint8_t> data)
{
  return impl_->foe_write(device_id, file_name, data);
}

FoEReadValue EthercatBus::foe_read(const DeviceId device_id, const std::string& file_name, std::span<uint8_t> buffer)
{
  return impl_->foe_read(device_id, file_name, buffer);
}

bool EthercatBus::change_device_state(const DeviceId device_id, const EthercatDeviceState target_state, bool blocking)
{
  // Convert into soem internal type
  const auto soem_state = map_to_soem_device_state(target_state);
  if (device_id != 0 && !impl_->has_device_on_bus(device_id)) {
    logging::warning(logger_) << "Cannot change device target state - device not on bus: " << device_id;
    return false;
  }
  // The timeout depends on the transition, so the current state is needed
  impl_->update_device_states();
  const auto timeout = transition_timeout(impl_->get_parameters(), impl_->get_device_state(device_id), soem_state);
  // Request the state change
  bool res = impl_->set_device_target_state(device_id, soem_state);
  if (!res) {
    logging::warning(logger_) << "Failed to set desired target state (" << target_state << ")"
                              << "for device: " << device_id << std::endl;
  }
  // Handle the case that the user does not want to block or that the set command failed
  if (!blocking || !res) {
    return res;
  }

  const bool success = impl_->wait_for_device_target_state(device_id, soem_state, timeout);
  if (!success) {
    logging::warning(logger_) << "Device went not into desired target state within timeout" << std::endl;
  }
  return success;
}

RegisterReadResult EthercatBus::read_register_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                                      const RegisterAddress address)
{
  return impl_->read_register_untyped(data, device_id, address);
}

RegisterWriteResult EthercatBus::write_register_untyped(std::span<const uint8_t> data, const DeviceId device_id,
                                                        const RegisterAddress address)
{
  return impl_->write_register_untyped(data, device_id, address);
}

EepromReadResult EthercatBus::read_eeprom_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                                  const EepromWordAddress word_address)
{
  return impl_->read_eeprom_untyped(data, device_id, word_address);
}

template <EthercatValueType T>
RegisterReadValue<T> EthercatBus::register_read(const DeviceId device_id, const RegisterAddress address)
{
  // This is the actual data instance we use
  T data{};
  // And this is just a safe representation (pointer + size) to it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(&data), sizeof(T));

  const auto result = read_register_untyped(buffer, device_id, address);
  if (!result) {
    return RegisterReadValue<T>(result);
  }
  return RegisterReadValue<T>(result, data);
}

template RegisterReadValue<bool> EthercatBus::register_read<bool>(DeviceId, RegisterAddress);
template RegisterReadValue<uint8_t> EthercatBus::register_read<uint8_t>(DeviceId, RegisterAddress);
template RegisterReadValue<int8_t> EthercatBus::register_read<int8_t>(DeviceId, RegisterAddress);
template RegisterReadValue<uint16_t> EthercatBus::register_read<uint16_t>(DeviceId, RegisterAddress);
template RegisterReadValue<int16_t> EthercatBus::register_read<int16_t>(DeviceId, RegisterAddress);
template RegisterReadValue<uint32_t> EthercatBus::register_read<uint32_t>(DeviceId, RegisterAddress);
template RegisterReadValue<int32_t> EthercatBus::register_read<int32_t>(DeviceId, RegisterAddress);
template RegisterReadValue<uint64_t> EthercatBus::register_read<uint64_t>(DeviceId, RegisterAddress);
template RegisterReadValue<int64_t> EthercatBus::register_read<int64_t>(DeviceId, RegisterAddress);
template RegisterReadValue<float> EthercatBus::register_read<float>(DeviceId, RegisterAddress);
template RegisterReadValue<double> EthercatBus::register_read<double>(DeviceId, RegisterAddress);

template <EthercatValueType T>
RegisterWriteResult EthercatBus::register_write(const DeviceId device_id, const RegisterAddress address, const T data)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data_wrap(reinterpret_cast<const uint8_t*>(&data), sizeof(T));
  return write_register_untyped(data_wrap, device_id, address);
}

template RegisterWriteResult EthercatBus::register_write<bool>(DeviceId, RegisterAddress, const bool);
template RegisterWriteResult EthercatBus::register_write<uint8_t>(DeviceId, RegisterAddress, const uint8_t);
template RegisterWriteResult EthercatBus::register_write<int8_t>(DeviceId, RegisterAddress, const int8_t);
template RegisterWriteResult EthercatBus::register_write<uint16_t>(DeviceId, RegisterAddress, const uint16_t);
template RegisterWriteResult EthercatBus::register_write<int16_t>(DeviceId, RegisterAddress, const int16_t);
template RegisterWriteResult EthercatBus::register_write<uint32_t>(DeviceId, RegisterAddress, const uint32_t);
template RegisterWriteResult EthercatBus::register_write<int32_t>(DeviceId, RegisterAddress, const int32_t);
template RegisterWriteResult EthercatBus::register_write<uint64_t>(DeviceId, RegisterAddress, const uint64_t);
template RegisterWriteResult EthercatBus::register_write<int64_t>(DeviceId, RegisterAddress, const int64_t);
template RegisterWriteResult EthercatBus::register_write<float>(DeviceId, RegisterAddress, const float);
template RegisterWriteResult EthercatBus::register_write<double>(DeviceId, RegisterAddress, const double);

DiagnosticsSnapshot EthercatBus::diagnostics(bool force_update)
{
  return impl_->diagnostics(force_update);
}

std::vector<MailboxEvent> EthercatBus::pop_mailbox_events()
{
  return impl_->pop_mailbox_events();
}

// Dispatch functions into a specific ethercat device
void EthercatBus::dispatch_device_update_write(EthercatDevice& device, const HighPrecisionTimeStamp& tp)
{
  device.update_write(tp);
}

void EthercatBus::dispatch_device_update_read(EthercatDevice& device, const HighPrecisionTimeStamp& tp)
{
  device.update_read(tp);
}

}  // namespace duatic::ecat_interface
