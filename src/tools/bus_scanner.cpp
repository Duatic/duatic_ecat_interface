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

#include <iostream>
#include <array>
#include <chrono>
#include <thread>
#include <cxxopts.hpp>

#include <duatic_ecat_interface/ethercat_bus.hpp>
#include <duatic_ecat_interface/internal/tools/sdo_print_helper.hpp>

using namespace duatic::ecat_interface;  // NOLINT(build/namespaces)

void handle_list_interfaces()
{
  std::cout << "Available buses:" << std::endl;

  const auto interfaces = EthercatBus::list_interfaces();

  for (const auto& interface : interfaces) {
    std::cout << "  " << interface << std::endl;
  }
}

void handle_scan(const std::string& interface)
{
  EthercatBus bus(EthercatBus::Parameters{ .interface = interface });
  std::cout << "scanning bus: " << interface << " for devices" << std::endl;

  const auto device_count = bus.initialize();
  std::cout << "found: " << device_count << " devices on the bus" << std::endl;

  const auto devices = bus.scan();

  for (const auto& info : devices) {
    std::cout << info << std::endl;
  }
}

void handle_sdo(const std::string& interface, const bool read_sdos)
{
  EthercatBus bus(EthercatBus::Parameters{ .interface = interface });

  const auto device_count = bus.initialize();
  std::cout << "found: " << device_count << " devices on the bus" << std::endl;

  const auto devices = bus.scan();

  for (const auto& info : devices) {
    const auto od = bus.read_od(info.id, true);

    std::cout << info << std::endl;
    for (const auto& sdo : od.entries()) {
      std::cout << "  [" << std::hex << "0x" << sdo.index << std::dec << "]"
                << ": " << sdo.name << std::endl;

      std::cout << "   object type: " << sdo.obj_type << std::endl;
      if (sdo.sub_entries.empty()) {
        std::cout << "   data type: " << sdo.data_type << std::endl;
        if (read_sdos) {
          std::cout << "   value: " << internal::sdo_print_helper(bus, info.id, sdo.index, 0, sdo.data_type)
                    << std::endl;
        }
      }

      for (const auto& sub : sdo.sub_entries) {
        std::cout << "     [0x" << std::hex << static_cast<int>(sub.index) << std::dec << "]: " << sub.name
                  << std::endl;
        std::cout << "      data_type: " << sub.data_type << std::endl;
        if (read_sdos) {
          std::cout << "      value: " << internal::sdo_print_helper(bus, info.id, sdo.index, sub.index, sub.data_type)
                    << std::endl;
        }
      }
    }
  }
}

int main(int argc, char** argv)
{
  cxxopts::Options options("duadrive_scanner", "Find and identify ");

  // clang-format off
    options.add_options()
        ("verb", "Actions to perform [scan, list_interfaces, sdo]", cxxopts::value<std::string>())
        ("b,bus", "Ethercat Bus", cxxopts::value<std::string>()->default_value("eth0"))
        ("read", "Actually read SDOs", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage");
  // clang-format on

  options.parse_positional({ "verb" });
  options.show_positional_help();
  options.set_width(200);

  const auto args = options.parse(argc, argv);

  // Check if help was passed or no verb was passed
  if (args.count("help") || !args.count("verb")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  const auto verb = args["verb"].as<std::string>();
  // Check if the right verb was passed
  if (verb != "scan" && verb != "list_interfaces" && verb != "sdo") {
    std::cout << "Please pass a valid action" << std::endl;
    std::cout << options.help() << std::endl;
    return -1;
  }

  try {
    if (verb == "list_interfaces") {
      handle_list_interfaces();
      return 0;
    }
    const auto bus = args["bus"].as<std::string>();

    if (verb == "scan") {
      handle_scan(bus);
      return 0;
    }

    if (verb == "sdo") {
      handle_sdo(bus, args["read"].as<bool>());
      return 0;
    }
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return -2;
  }

  return 0;
}
