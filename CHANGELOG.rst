^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Changelog for package duatic_ecat_interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

2.0.0 (2026-11-04)
------------------
* refactor!: rename the package to duatic_ecat_interface (#24)
* feat(executor): add is_spinning to the bus executor (#20)
* feat(diagnostics): report the device topology read from SOEM (#17)
* feat(diagnostics): extend the bus diagnostics (#18)
* feat(nic): add nic::is_up, telling whether the selected interface is connected (#16)
* feat(bus_scanner): add an --sdo option that reads and prints SDOs
* feat: add reset_hooks to deregister a device's callbacks
* feat: give the EtherCAT bus its own logger
* feat: log failed device state changes
* feat: double the default PDO buffer size to 8192 bytes
* fix: resolve gcc compile errors on humble (#23)
* fix: rework the device ownership model, dropping the previously virtual functions (#15)
* fix: check string SDO reads for a zero terminator (#14)
* fix(diagnostics): remove a locking issue in the diagnostics (#18)
* fix(bus_scanner): read SDOs correctly with --sdo
* fix: clamp FoE reads to the size of the buffer
* fix: clear the devices only after the on_post_shutdown callbacks ran
* fix: iterate over the devices with an unsigned index
* fix: rename priorty_inheriting_mutex.hpp to priority_inheriting_mutex.hpp
* fix: use the SPDX license identifier BSD-3-Clause
* refactor: make EthercatDeviceBase neither copyable nor movable
* refactor: make EthercatBus final, and have SingleBusExecutor take its bus by value

1.0.0 (2026-08-20)
------------------
* First public release
