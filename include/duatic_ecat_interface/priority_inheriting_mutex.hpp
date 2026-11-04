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
 */

#pragma once

#include <pthread.h>

#include <cassert>
#include <cerrno>
#include <system_error>

namespace duatic::ecat_interface
{

/**
 * @brief A non-recursive mutex that donates the priority of a blocked waiter to the current owner
 * @note This only has an effect if the rt thread actually runs under SCHED_FIFO / SCHED_RR. Linux does not
 * propagate nice values between SCHED_OTHER threads, so under the default scheduler this behaves like std::mutex.
 */
class PriorityInheritingMutex
{
public:
  using native_handle_type = pthread_mutex_t*;

  PriorityInheritingMutex()
  {
    pthread_mutexattr_t attr;
    if (const int err = pthread_mutexattr_init(&attr); err != 0) {
      throw std::system_error(err, std::system_category(), "pthread_mutexattr_init");
    }
    // Enables the kernel side rt-mutex handling (FUTEX_LOCK_PI on linux)
    const int protocol_err = pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    const int init_err = (protocol_err == 0) ? pthread_mutex_init(&mutex_, &attr) : 0;
    pthread_mutexattr_destroy(&attr);

    if (protocol_err != 0) {
      throw std::system_error(protocol_err, std::system_category(), "pthread_mutexattr_setprotocol(PRIO_INHERIT)");
    }
    if (init_err != 0) {
      throw std::system_error(init_err, std::system_category(), "pthread_mutex_init");
    }
  }

  ~PriorityInheritingMutex()
  {
    pthread_mutex_destroy(&mutex_);
  }

  PriorityInheritingMutex(const PriorityInheritingMutex&) = delete;
  PriorityInheritingMutex& operator=(const PriorityInheritingMutex&) = delete;

  void lock()
  {
    // Can only fail on misuse (EDEADLK / EINVAL) - not on contention
    if (const int err = pthread_mutex_lock(&mutex_); err != 0) {
      throw std::system_error(err, std::system_category(), "pthread_mutex_lock");
    }
  }

  /**
   * @brief try_lock - acquire the lock without blocking
   * @return false if the mutex is currently held by another thread
   */
  bool try_lock()
  {
    const int err = pthread_mutex_trylock(&mutex_);
    if (err == 0) {
      return true;
    }
    if (err == EBUSY) {
      return false;
    }
    throw std::system_error(err, std::system_category(), "pthread_mutex_trylock");
  }

  void unlock() noexcept
  {
    const int err = pthread_mutex_unlock(&mutex_);
    assert(err == 0 && "unlocking a mutex that is not owned by this thread");
    (void)err;
  }

  native_handle_type native_handle() noexcept
  {
    return &mutex_;
  }

private:
  pthread_mutex_t mutex_{};
};

}  // namespace duatic::ecat_interface
