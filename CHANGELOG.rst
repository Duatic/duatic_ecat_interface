2.0.0 (2026-11-04)
------------------
* refactor!: rename the package to duatic_ecat_interface (#24)
* feat(executor): added is_spinning method to the bus executor (#20)
* fix: resolve gcc compile errors on humble (#23)
* fix(pre-commit): use mirrors-clang-format instead of system binary (#13)
* double default pdo buffer size
* Feature/extend diagnostics (#18)
* fix some loop inconsitency
* Feature/report device topology (#17)
* Added nic::is_up functionality to determine if the selected interface is connected (#16)
* run pre-commit
* fix sdo readout option in the bus scanner
* added --sdo option to bus_scanner tool - allows to actually read and print sdos
* fix minor issue with the foe read used sizes
* fix dead on_post_shutdown callbacks
* added reset_hooks function
* fix the device owner ship model  (#15)
* make EthercatDeviceBase non copyable and non movable
* String SDO reads need to check for zero terminators (#14)
* gave the ethercat-bus its own logger
* add a few logging statements in the change_device_state method
* added changelog
* Several small consistency fixes
* added suggested clang format fix
* added gitignore file
* fix typo in file name
* disable flacky test
* fix license identifier
