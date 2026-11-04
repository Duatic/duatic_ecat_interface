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

#include <stdexcept>
#include <optional>
#include <string>

#include "duatic_ecat_interface/backend.hpp"

namespace duatic::ecat_interface
{
/**
 * @brief BackendError - Exception which represents and error which comes for a specific backend
 */
class BackendError : public std::runtime_error
{
public:
  explicit BackendError(const std::string& msg, Backend backend = Backend::Unknown,
                        std::optional<int> ec = std::nullopt)
    : std::runtime_error(msg), backend_(backend), ec_(ec)
  {
    full_msg_ = "[" + to_string(backend) + "] " + msg;
    if (ec) {
      full_msg_ += " (ec: " + std::to_string(ec.value()) + ")";
    }
  }

  const char* what() const noexcept override
  {
    return full_msg_.c_str();
  }

  /**
   * @brief get_backend - obtain the backend identifier of the used backend
   */
  Backend get_backend() const noexcept
  {
    return backend_;
  }

  /**
   * @brief get_error_code - obtain the (backend specific) error code of the specific implementation
   */
  std::optional<int> get_error_code() const noexcept
  {
    return ec_;
  }

private:
  Backend backend_{ Backend::Unknown };
  std::optional<int> ec_{};

  std::string full_msg_;
};

class DeviceNotFound : public BackendError
{
public:
  explicit DeviceNotFound(const std::string& msg, Backend backend = Backend::Unknown,
                          std::optional<int> ec = std::nullopt)
    : BackendError(msg, backend, ec)
  {
  }
};

class DeviceConfigurationError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class ExecutorError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};
}  // namespace duatic::ecat_interface
