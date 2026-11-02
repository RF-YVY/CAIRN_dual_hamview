#pragma once

#include "Lock.h"

namespace concurrency
{
class LockGuard
{
  public:
    explicit LockGuard(Lock *lock) : mutex(lock) { mutex->lock(); }
    ~LockGuard() { mutex->unlock(); }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

  private:
    Lock *mutex;
};
} // namespace concurrency
