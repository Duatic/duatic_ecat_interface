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

#include "duatic_ecat_interface/ethercat_interface.hpp"

/**
 * This is a minimal example which demonstrates how to use the api
 */

using namespace duatic::ecat_interface;  // NOLINT(build/namespaces)

struct RX
{
  int a;
};

struct TX
{
  int b;
};

class Drive
{
public:
  using Device = EthercatDevice;
  using DevicePtr = std::shared_ptr<Device>;

  explicit Drive(DevicePtr device) : device_(std::move(device))
  {
  }

  auto& get_device()
  {
    return device_;
  }

  void do_something()
  {
  }

  int get_reading()
  {
    device_->get_rx_pdo<RX>();
    return 0;
  }

  void set_command(int cmd)
  {
  }

private:
  // Idea is to use composition instead of inheritence
  DevicePtr device_{};
};

std::unique_ptr<Drive> drive;
DeviceId device_id = 1;

int main(void)
{
  // This should happen in the same thread
  auto bus = std::make_shared<EthercatBus>(EthercatBus::Parameters{});
  bus->initialize();

  // API without device usage:
  std::array<uint8_t, 8> data;
  bus->read_sdo_untyped(data, 1, 1, 0);

  drive = std::make_unique<Drive>(bus->acquire_device(device_id));

  bus->startup();
  bus->activate();

  // Executor pattern as we know it from ros (2nd thread)
  SingleBusExecutor executor{ bus };
  executor.spin();

  // OR
  while (true) {
    bus->update_rt();
  }
  // thread 2
  {
    bus->update_service();
    sleep(20);  // sleep 20ms
  }
}

void second_thread()
{
  // Asynchronous sdo read/writes
  drive->do_something();

  // Or access readings
  drive->get_reading();

  // Or write commands
  drive->set_command(1);
}
