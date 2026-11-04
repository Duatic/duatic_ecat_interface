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

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <csignal>

#include <duatic_message_logger/log.hpp>

namespace duatic::ecat_interface
{
/**
 * @brief set_realtime_priority - try to configure the passed thread to have higher priorities and behave more rt
 * capable therefore
 * @param thread - the thread to configure
 * @param priority - the thread prioty between [1-99] with 99 beeing the highest priority
 * @param cpu_coure - the cpu core to tie this thread to. Pass -1 to keep the default
 * @param scheduler - the scheduler type to choose for this thread. Reasonable options are [SCHED_FIFO, SCHED_RR]
 *
 * This function tries to make the passed thread more realtimy by doing three things:
 * 1. Configure the scheduler to a more realtime like scheduler. E.g. SCHED_FIFO
 * 2. Increase the priority of the thread
 * 3. Bind the thread to a specific core. This avoids overhead by moving the thread between cores (cache misses)
 *
 * @return true in case of success, false in case of error
 *
 * You will need to allow your current user + your container to set these settings
 */
inline bool set_realtime_priority(pthread_t thread, int priority = 60, int cpu_core = -1, int scheduler = SCHED_FIFO)
{
  if (priority < 1 || priority > 99) {
    logging::error() << "Failed to configure scheduler - pass a valid priority between [1,99]";
    return false;
  }

  // Configure the scheduler to the desired type and priority
  sched_param param;
  param.sched_priority = priority;
  if (auto ec = pthread_setschedparam(thread, scheduler, &param); ec != 0) {
    logging::error() << "Failed to configure scheduler (could not set priority and scheduler type) for thread: "
                     << thread << ". Check limits.conf or execute as root."
                     << " ec: " << ec;
    return false;
  }

  // Now try to attach the thread to a specific cpu core
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  const auto number_of_cpus = sysconf(_SC_NPROCESSORS_ONLN);

  // User did not want to attach thread to a cpu core
  if (cpu_core < 0) {
    return true;
  }

  if (cpu_core >= number_of_cpus) {
    logging::error() << "Failed to bind thread: " << thread << " to cpu core: " << cpu_core
                     << ". Please pass a valid cpu core (max: " << number_of_cpus << ")" << std::endl;
    return false;
  }

  CPU_SET(static_cast<std::size_t>(cpu_core), &cpuset);
  if (auto ec = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset); ec != 0) {
    logging::error() << "Failed to bind thread: " << thread << " to cpu core: " << cpu_core << ". ec: " << ec;
    return false;
  }
  return true;
}

/**
 * @brief set_realtime_priority - variant which configures the current thread
 * See set_realtime_priority with pthread parameter for the full explaination
 */
inline bool set_realtime_priority(int priority = 60, int cpu_core = -1, int scheduler = SCHED_FIFO)
{
  return set_realtime_priority(pthread_self(), priority, cpu_core, scheduler);
}

/**
 * @brief block_signals_current_thread - block asynchronous signal delivery to the calling thread
 * @param previous_mask - optional out parameter receiving the mask in effect before the call,
 *        for later restoration via restore_signal_mask. Pass nullptr if not needed.
 *
 * A thread with signals blocked never returns EINTR from a blocking syscall, which removes a
 * source of jitter from cyclic real-time loops. Some other thread in the process must then
 * handle termination signals - the usual pattern is one dedicated thread calling sigwait().
 *
 * Synchronous signals are deliberately left unblocked: they are raised by the faulting
 * instruction itself, blocking them is undefined behaviour, and on Linux the kernel forces
 * the default action regardless, so a genuine fault would kill the process without reaching
 * a handler or a debugger.
 *
 * SIGKILL and SIGSTOP cannot be blocked; they are silently ignored in the requested set.
 *
 * @return true in case of success, false in case of error
 */
inline bool block_signals_current_thread(sigset_t* previous_mask = nullptr)
{
  sigset_t set;
  if (sigfillset(&set) != 0) {
    logging::error() << "Failed to build the signal set. errno: " << errno;
    return false;
  }

  // list of still allowed signals
  for (const int sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP, SIGSYS, SIGABRT }) {
    sigdelset(&set, sig);
  }

  // pthread_sigmask reports failure through its return value and does not set errno
  if (const auto ec = pthread_sigmask(SIG_BLOCK, &set, previous_mask); ec != 0) {
    logging::error() << "Failed to block signals for the current thread. ec: " << ec;
    return false;
  }
  return true;
}

/**
 * @brief restore_signal_mask - reinstate a mask previously captured by block_signals_current_thread
 * @param mask - the mask to install for the calling thread
 * @return true in case of success, false in case of error
 */
inline bool restore_signal_mask(const sigset_t& mask)
{
  if (const auto ec = pthread_sigmask(SIG_SETMASK, &mask, nullptr); ec != 0) {
    logging::error() << "Failed to restore the signal mask of the current thread. ec: " << ec;
    return false;
  }
  return true;
}
}  // namespace duatic::ecat_interface
