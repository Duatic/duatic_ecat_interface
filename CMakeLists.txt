cmake_minimum_required(VERSION 3.22)
project(duatic_ecat_interface)

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_ros REQUIRED)

set(PACKAGE_DEPENDENCIES
  soem_vendor
  #etherlab_master_vendor
  duatic_message_logger
)

foreach(Dependency IN ITEMS ${PACKAGE_DEPENDENCIES})
  find_package(${Dependency} REQUIRED)
endforeach()


add_library(duatic_ecat_interface
  src/internal/soem/soem_backend.cpp
  src/internal/nic.cpp
  src/ethercat_device.cpp
  src/bus_diagnostics.cpp
)
add_library(duatic_ecat_interface::duatic_ecat_interface ALIAS duatic_ecat_interface)
target_compile_features(duatic_ecat_interface PUBLIC c_std_99 cxx_std_20)  # Require C99 and C++20
target_include_directories(duatic_ecat_interface PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>)


target_link_libraries(duatic_ecat_interface duatic_message_logger::duatic_message_logger soem_vendor::soem_vendor)
# enable additional warnings
target_compile_options(duatic_ecat_interface PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wnon-virtual-dtor
  -Woverloaded-virtual
  -Wimplicit-fallthrough
  -Wduplicated-cond
  -Wconversion
  -Wundef
  -Wformat=2
  -Wsign-conversion
  -Wnull-dereference
  -Werror=return-type
  -Wuseless-cast
  -Wswitch-enum
  -Wlogical-op
)

# Causes the visibility macros to use dllexport rather than dllimport,
# which is appropriate when building the dll but not consuming it.
target_compile_definitions(duatic_ecat_interface PRIVATE "duatic_ecat_interface_BUILDING_LIBRARY")

# Demo
add_executable(demo ./src/example/demo.cpp)
target_compile_features(demo PUBLIC c_std_99 cxx_std_20)
target_include_directories(demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(demo duatic_ecat_interface)

# Tooling
find_package(cxxopts REQUIRED)
add_executable(bus_scanner ./src/tools/bus_scanner.cpp)
target_compile_features(bus_scanner PUBLIC c_std_99 cxx_std_20)
target_include_directories(bus_scanner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bus_scanner duatic_ecat_interface cxxopts::cxxopts)



# Installation

# exclude the "internal" folder which is not part of the public API
install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
  REGEX "duatic_ecat_interface/internal(/|$)" EXCLUDE
)

install(
  TARGETS duatic_ecat_interface
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  TARGETS bus_scanner
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_precision_update_rate
    test/test_precision_update_rate.cpp
  )
  ament_add_gtest(test_od
    test/test_od.cpp
  )
  ament_add_gtest(test_value_diagnostics_wrapper
    test/test_value_diagnostics_wrapper.cpp
  )
  ament_add_gtest(test_network
    test/test_network.cpp
  )
  ament_add_gtest(test_bus_state
    test/test_bus_state.cpp
  )
  ament_add_gtest(test_executor
    test/test_executor.cpp
  )
  ament_add_gtest(test_api
    test/test_api.cpp
  )

  target_link_libraries(test_precision_update_rate
    duatic_ecat_interface
  )
  target_link_libraries(test_od
    duatic_ecat_interface
  )
  target_link_libraries(test_value_diagnostics_wrapper
    duatic_ecat_interface
  )
  target_link_libraries(test_network
    duatic_ecat_interface
  )
  target_link_libraries(test_bus_state
    duatic_ecat_interface
  )
  target_link_libraries(test_executor
    duatic_ecat_interface
  )
  target_link_libraries(test_api
    duatic_ecat_interface
  )
endif()

ament_export_include_directories(
  "include/${PROJECT_NAME}"
)
ament_export_libraries(
  duatic_ecat_interface
)
ament_export_targets(
  export_${PROJECT_NAME}
)

ament_export_dependencies(${PACKAGE_DEPENDENCIES})



ament_package()
