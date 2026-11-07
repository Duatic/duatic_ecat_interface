# duatic_ecat_interface

A multibackend C++20 EtherCAT®¹ master library.

[![Humble](https://github.com/Duatic/duatic_ecat_interface/raw/badges/humble.svg)](https://github.com/Duatic/duatic_ecat_interface/actions/workflows/ci.yml)
[![Jazzy](https://github.com/Duatic/duatic_ecat_interface/raw/badges/jazzy.svg)](https://github.com/Duatic/duatic_ecat_interface/actions/workflows/ci.yml)
[![Kilted](https://github.com/Duatic/duatic_ecat_interface/raw/badges/kilted.svg)](https://github.com/Duatic/duatic_ecat_interface/actions/workflows/ci.yml)
[![Lyrical](https://github.com/Duatic/duatic_ecat_interface/raw/badges/lyrical.svg)](https://github.com/Duatic/duatic_ecat_interface/actions/workflows/ci.yml)
[![Rolling](https://github.com/Duatic/duatic_ecat_interface/raw/badges/rolling.svg)](https://github.com/Duatic/duatic_ecat_interface/actions/workflows/ci.yml)

This library supports different EtherCAT® master library backends depending on the users need.


# License

The contents are licensed under the BSD-3-Clause  [license](LICENSE).\
Images in this repository are to be licensed separately if you want to use them for any other usecase than forking this repository. Please open an issue in order to get in touch with us.
3D models may only be used for the sole purpose of operating a robotics stack. It is explicitly not allowed to use them for reproduction of any parts.

## EtherCAT® Master License

The EtherCAT Technology, and the trade name "EtherCAT" are the intellectual property of, and protected by Beckhoff Automation GmbH. You may review, edit, compile, link, debug the EtherCAT Master Code, provided that before implementing into a product, using, selling or otherwise distributing an EtherCAT Master License is obtained from Beckhoff Automation GmbH & Co.KG.

To receive a copy of the EtherCAT Master License contact Beckhoff Automation GmbH & Co.KG, Huelshorstweg 20, D-33415 Verl, Germany ([www.beckhoff.com](https://www.beckhoff.com)).

# Dependencies

All dependencies with their corresponding version are listed in the [repos.list](./repos.list).

| Name | Description | License
| ---  | --- | --- |
| [duatic_soem_vendor](https://github.com/Duatic/soem_vendor) | SOEM as a plain C library | BSD-3-Clause, GPLv2 with Linktime exception |
| [duatic_message_logger](https://github.com/Duatic/duatic_message_logger) | Logging library which allows logging with and without ROS | BSD-3-Clause |
| [cxxopts](https://github.com/jarro2783/cxxopts) | Modern C++ CLI interface library | MIT |


# Contributing

Please see the [Contributing guide](./CONTRIBUTING.md)

---

¹ EtherCAT® is a registered trademark and patented technology, licensed by Beckhoff Automation GmbH, Germany.
