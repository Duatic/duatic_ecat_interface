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
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <memory>
#include <condition_variable>

#include "duatic_ecat_interface/precision_update_rate.hpp"
#include "duatic_ecat_interface/priority_inheriting_mutex.hpp"
#include "duatic_ecat_interface/exceptions.hpp"
#include "duatic_ecat_interface/realtime_utils.hpp"
#include "duatic_ecat_interface/ethercat_bus.hpp"
#include "duatic_ecat_interface/bus_diagnostics.hpp"

namespace duatic::ecat_interface
{

struct ExecutorParameters
{
  // priority and desired cpu core for the update thread
  int realtime_priority{ 60 };
  int desired_cpu_core{ -1 };
  // period at which the service thread is spun
  std::chrono::microseconds service_thread_period{ 2010 };
};

/**
 * @brief SingleBusExecutor - an executor which performs the rate limited update on one bus
 * A realtime thread calls EthercatBus::update_rt() every cycle, a service thread calls EthercatBus::update_service()
 * @note the executor owns the end of the bus lifecycle: stop() - and therefore the destructor - shuts the bus down,
 * although initialize() / startup() / activate() are called by the user
 */
class SingleBusExecutor
{
public:
  explicit SingleBusExecutor(std::shared_ptr<EthercatBus> bus, const ExecutorParameters& params = ExecutorParameters{})
    : bus_(bus)
    , params_(params)
    , update_rate_(bus->get_parameters().cycle_time)
    , logger_(logging::get_logger_with_default_sink("SingleBusExecutor"))
  {
    // The cycle time is the period of the realtime thread, also without distributed clocks
    if (bus->get_parameters().cycle_time <= std::chrono::nanoseconds{ 0 }) {
      throw ExecutorError("The bus cycle time (cycle_time) has to be greater than zero to spin the bus");
    }
  }

  ~SingleBusExecutor()
  {
    stop();
  }

  /**
   * @brief spin - start spinning (calling the updates of) the bus
   * @note the bus is shut down once the executor stops, see stop()
   */
  void spin()
  {
    if (spinning_) {
      throw ExecutorError("Executor already spinning");
    }
    logging::info(logger_) << "Setting up Executor with update frequency: "
                           << 1.0 / std::chrono::duration_cast<std::chrono::duration<double>>(
                                        bus_->get_parameters().cycle_time)
                                        .count()
                           << "Hz";

    spinning_ = true;
    rt_thread_ = std::jthread([&](std::stop_token stoken) {
      try {
        if (!set_realtime_priority(params_.realtime_priority, params_.desired_cpu_core)) {
          logging::error(logger_) << "Failed to set realtime priority of spin thread - run as root or configure "
                                     "security.limits";
        }
        rt_thread_diagnostics_.is_running = true;
        publish_diagnostics(rt_thread_diagnostics_, published_rt_thread_diagnostics_, LockMode::Block);

        while (!stoken.stop_requested()) {
          const auto start = HighPrecisionClock::now();

          // In case the distributed clock is enabled we can synchronize our clock accordingly
          // The bus reports an offset we then feed into the PrecisionUpdateRate
          // In case dc is disabled std::nullopt is returned
          const auto dc_correction_offset = bus_->update_rt();

          update_timing_diagnostics(rt_thread_diagnostics_, start, HighPrecisionClock::now());
          rt_thread_diagnostics_.missed_rate_steps = update_rate_.overrun_count();
          rt_thread_diagnostics_.accumulated_delay = update_rate_.accumulated_delay();
          publish_diagnostics(rt_thread_diagnostics_, published_rt_thread_diagnostics_, LockMode::TryOnce);

          if (this->update_rate_.step(dc_correction_offset.value_or(std::chrono::nanoseconds{ 0 }))) {
            logging::warning(logger_) << "Could not keep update rate: " << this->update_rate_.last_delay().count()
                                      << " ns";
          }
        }
      } catch (std::exception& ex) {
        logging::fatal(logger_) << "Caught exception in RT thread: " << ex.what();
        rt_error_ptr_ = std::current_exception();
        rt_error_occurred_.store(true, std::memory_order_release);
      } catch (...) {
        logging::fatal(logger_) << "Caught unknown exception in RT thread";
        rt_error_ptr_ = std::current_exception();
        rt_error_occurred_.store(true, std::memory_order_release);
      }
      rt_thread_diagnostics_.is_running = false;
      publish_diagnostics(rt_thread_diagnostics_, published_rt_thread_diagnostics_, LockMode::Block);
    });

    service_thread_ = std::jthread([&](std::stop_token stoken) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock lk(m);

      try {
        service_thread_diagnostics_.is_running = true;
        publish_diagnostics(service_thread_diagnostics_, published_service_thread_diagnostics_, LockMode::Yield);
        while (!stoken.stop_requested()) {
          const auto start = HighPrecisionClock::now();
          if (!bus_->update_service()) {
            // The bus can indicate that no service spin is needed anymore
            logging::info(logger_) << "Bus indicated that service thread is not needed anymore - stopping";
            break;
          }

          update_timing_diagnostics(service_thread_diagnostics_, start, HighPrecisionClock::now());
          publish_diagnostics(service_thread_diagnostics_, published_service_thread_diagnostics_, LockMode::Yield);
          // We need less precise update rates for the service thread which is why we go for a simple condition variable
          cv.wait_for(lk, stoken, params_.service_thread_period, [] { return false; });
        }
      } catch (std::exception& ex) {
        logging::fatal(logger_) << "Caught exception in Service thread: " << ex.what();
        service_error_ptr_ = std::current_exception();
        service_error_occurred_.store(true, std::memory_order_release);
      } catch (...) {
        logging::fatal(logger_) << "Caught unknown exception in Service thread";
        service_error_ptr_ = std::current_exception();
        service_error_occurred_.store(true, std::memory_order_release);
      }
      service_thread_diagnostics_.is_running = false;
      publish_diagnostics(service_thread_diagnostics_, published_service_thread_diagnostics_, LockMode::Yield);

      logging::info(logger_) << "Service thread is ending now" << std::endl;
    });
  }

  /**
   * @brief stop - stop spinning the bus and shut it down
   */
  void stop()
  {
    if (!spinning_) {
      return;
    }
    if (rt_thread_.joinable()) {
      rt_thread_.request_stop();
      rt_thread_.join();
    }
    if (service_thread_.joinable()) {
      service_thread_.request_stop();
      service_thread_.join();
    }
    spinning_ = false;
    // Important to not call shutdown in the spinning thread
    bus_->shutdown();
  }

  /**
   * @brief full_diagnostics - obtain a full diagnostics snapshot of the ethercat bus and include executor diagnostics
   * @note see @ref EthercatBus::diagnostics for parameter meaning
   * @note intended for consumer threads with a lower priority than the realtime thread
   */
  DiagnosticsSnapshot full_diagnostics(bool force_update = false)
  {
    auto snapshot = bus_->diagnostics(force_update);
    lock_by_polling(diagnostics_mutex_);
    std::lock_guard<PriorityInheritingMutex> lock(diagnostics_mutex_, std::adopt_lock);
    snapshot.executor = ExecutionStatus{ .is_spinning = spinning_,
                                         .rt_thread_stats = published_rt_thread_diagnostics_,
                                         .service_thread_stats = published_service_thread_diagnostics_ };
    return snapshot;
  }

  /**
   * @brief has_rt_error - check if a critical error in the realtime loop has occurred
   */
  bool has_rt_error() const
  {
    return rt_error_occurred_.load(std::memory_order_acquire);
  }

  /**
   * @brief get_rt_error - get the error that has occurred in the rt thread
   */
  std::exception_ptr get_rt_error() const
  {
    // The flag is set after the error, so the error is complete once the flag is visible
    return has_rt_error() ? rt_error_ptr_ : nullptr;
  }

  /**
   * @brief has_service_error - check if a critical error in the service loop has occurred
   */
  bool has_service_error() const
  {
    return service_error_occurred_.load(std::memory_order_acquire);
  }

  /**
   * @brief get_service_error - get the error that has occurred in the service thread
   */
  std::exception_ptr get_service_error() const
  {
    // The flag is set after the error, so the error is complete once the flag is visible
    return has_service_error() ? service_error_ptr_ : nullptr;
  }

  /**
   * @brief is_spinning - check if the executor is currently spinning the rt thread at least
   */
  bool is_spinning() const
  {
    return spinning_;
  }

private:
  std::exception_ptr rt_error_ptr_;
  std::atomic_bool rt_error_occurred_{ false };

  std::exception_ptr service_error_ptr_;
  std::atomic_bool service_error_occurred_{ false };
  std::shared_ptr<EthercatBus> bus_;
  const ExecutorParameters params_;
  std::jthread rt_thread_;
  std::jthread service_thread_;
  PrecisionUpdateRate update_rate_;
  logging::Logger logger_;
  // Read by other threads through is_spinning() / full_diagnostics()
  std::atomic_bool spinning_{ false };

  // Working copies, each only accessed by its own thread
  alignas(64) ExecutorTimingDiagnostics rt_thread_diagnostics_;
  alignas(64) ExecutorTimingDiagnostics service_thread_diagnostics_;
  // Copies for other threads (full_diagnostics()), guarded by diagnostics_mutex_
  // Priority inheriting as the RT thread blocks on it once before and once after its loop
  PriorityInheritingMutex diagnostics_mutex_;
  ExecutorTimingDiagnostics published_rt_thread_diagnostics_;
  ExecutorTimingDiagnostics published_service_thread_diagnostics_;

  enum class LockMode
  {
    // RT thread in its loop: skip publishing while the lock is held, the next cycle publishes again
    TryOnce,
    // RT thread before / after its loop: block, a holder inherits the RT priority
    Block,
    // Any other thread: never wait on the lock in the kernel, see lock_by_polling
    Yield
  };

  // Copies a thread's working diagnostics for other threads
  void publish_diagnostics(const ExecutorTimingDiagnostics& working, ExecutorTimingDiagnostics& published,
                           const LockMode mode)
  {
    std::unique_lock<PriorityInheritingMutex> lock(diagnostics_mutex_, std::defer_lock);
    switch (mode) {
      case LockMode::TryOnce:
        if (!lock.try_lock()) {
          return;
        }
        break;
      case LockMode::Block:
        lock.lock();
        break;
      case LockMode::Yield:
        lock_by_polling(diagnostics_mutex_);
        lock = std::unique_lock<PriorityInheritingMutex>(diagnostics_mutex_, std::adopt_lock);
        break;
    }
    published = working;
  }

  static constexpr void update_timing_diagnostics(ExecutorTimingDiagnostics& diag,
                                                  const HighPrecisionTimeStamp& start_tp,
                                                  const HighPrecisionTimeStamp& after_update_tp)
  {
    using namespace std::chrono;  // NOLINT(build/namespaces)
    // First diagnostics is -> at which interval was the update called
    diag.last_update_period = duration_cast<microseconds>(start_tp - diag.last_update_tp);
    diag.average_update_rate =
        0.5 * (1.0 / duration_cast<duration<double>>(diag.last_update_period).count()) + 0.5 * diag.average_update_rate;
    diag.last_update_tp = start_tp;
    // Second diagnostics is -> how long took the update
    diag.last_update_duration = duration_cast<microseconds>(after_update_tp - start_tp);
    diag.average_update_duration =
        0.5 * duration_cast<duration<double>>(diag.last_update_duration).count() + 0.5 * diag.average_update_duration;
  }
};
}  // namespace duatic::ecat_interface
